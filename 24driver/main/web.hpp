#pragma once

#include "timing.hpp"
#include "byte_encoding.hpp"
#include "memory.hpp"
#include "state.hpp"
#include "pid.hpp"
#include "power.hpp"


#include "freertos/queue.h"
#include "WiFi.h"
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h"
#include "AsyncWebSocket.h"

#include <string.h>
#include <unordered_map>
#include <cassert>


namespace web {
  // AsyncWebServer Bug
  // ------------------
  // TODO: maybe switch to arduino websockets?
  //
  // The ESP32 randomly restarts when clients are disconnecting. Probably some stray pointer...
  // The first line of the error shows up as "Guru Meditation Error: Core  0 panic'ed (LoadProhibited".
  // Similar problem to [this issue](https://github.com/me-no-dev/ESPAsyncWebServer/issues/325).


  // WiFi Bug
  // --------
  // TODO: potentially fix wifi connection bug by upgrading to esp-idf-v4
  //
  // Can't switch from AP mode to STA. The docs says we should turn wifi off,
  // but when trying to do that we get an error about `wifi not start`...
  //
  // There's some WiFi bugs in esp-idf-v3.2 that seem to be fixed in v3.3 and
  // v4. Unfortunately the esp32-arduino library support up to v3.2 for now...
  // As a work around, instead of switching connection on the fly, we'll save
  // the new settings and restart the chip... Seems that connecting on startup
  // works better than switching.
  //
  // For now use a `needs_restart` flag to switch.

  enum APICodes : uint8_t {

    STATE = 0x00, // State measurements and power.
    COMMAND = 0x01, // Command to position, or drive power.
    CONFIGURATION = 0x02, // Configuration (max limits, direction, PID params).
    CONFIGURE = 0x03, // Set configuration.
    SCAN_NETWORKS = 0x04, // Scan for wifi networks in range.
    AVAILABLE_NETOWRKS = 0x05, // Reply with networks in range.
    CONNECT_NETWORK = 0x06, // Connect to network or start access point.
    REQUEST_STATE_UPDATES = 0x07, // Register for state updates.
    REQUEST_CONFIGURATION = 0x08, // Ask for configuration.
    RELOAD_CONFIGURATION = 0x09, // Reload and ask for configuration.

  };

  const size_t max_length = 256;
  // Default SSID and password when creating an Access Point.
  char ap_ssid[max_length] = "ESP32-24Driver";
  char ap_password[max_length] = "give me a hand";
  // SSID and password when connecting to an existing network.
  char router_ssid[max_length] = "";
  char router_password[max_length] = "";
  // Whether to connect to a router or start an AP; always fallback on AP.
  bool connect_to_router = false;
  // Whether we actually conencted to router or AP.
  bool connected_to_router = false;
  // Whether new connection needs to be established, and new settings saved.
  bool new_settings = false;

  // Whether we need to save/reload state config.
  bool save_config = false;
  bool reload_config = false;


  // Server port to use, 80 for HTTP/WS or 443 for HTTPS/WSS.
  const int PORT = 80;

  // Don't update faster than 100ms.
  const unsigned long min_web_update_period = 100;

  // IP address to get to the server.
  IPAddress ip = {};

  // Whether wifi and everything was initialized without error.
  bool ok = false;

  // Status whilst wifi is not ok.
  const char * status = "Connecting WiFi...";

  // Loop timing for the web update.
  timing::LoopTimer timer = {};


  // Web servers, and web socket handler.
  AsyncWebServer server(PORT);
  AsyncWebSocket ws("/ws");

  typedef uint32_t ws_client_id;

  // Replying with available networks takes a lot of time to finish scanning.
  // We'll schedule scans on the update loop, and reply to all clients in queue.
  QueueHandle_t clients_waiting_networks = {};

  // To get the state, a client should register for it every 100 ms. It then gets
  // state updates as fast as the web server updates, without requesting each of them.
  const unsigned long register_duration = 200;
  QueueHandle_t clients_waiting_state = {};
  std::unordered_map<ws_client_id, unsigned long> state_register_time;

  // Reply to clients requesting config from the main loop.
  QueueHandle_t clients_waiting_config = {};


  // Commands are sent by clients, they may overlap, allow only 1 to have control.
  const IPAddress default_ip = {};
  IPAddress last_command_ip = {};
  unsigned long last_command_time = 0;
  // Reserve control for 1 client for 200ms.
  const unsigned long max_command_time = 200;

  // Use bit banging utils.
  using namespace byte_encoding;

  // Send data or ditch client if queue is full. Fail fast, eh!
  inline void send_binary(uint32_t id, const char * message, size_t len){
    auto client = ws.client(id);

    // Skip if client was closed and deleted.
    if (client == nullptr) return;


    // Client is open, check if we can send.
    bool ok = (client->status() == WS_CONNECTED) and client->canSend();

    // Send message if we can.
    if (ok) client->binary(message, len);
    // Otherwise ditch connection and await reconnect.
    else client->close();
  }

  inline void send_binary(uint32_t id, uint8_t * message, size_t len) {
    send_binary(id, reinterpret_cast<const char *>(message), len);
  }


  void update_commands(){
    // No updates if no command is set.
    if (last_command_ip == default_ip) return;
    // Nothing to update if we're still within the command holding period.
    if (millis() - last_command_time < max_command_time) return;

    // We're past the hold time on the last command, reset outputs.
    state::halt_drivers();

    last_command_ip = default_ip;
  }

  // Handle websocket events.
  void on_ws_event(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t * data, size_t len) {
    switch(type){
      case WS_EVT_CONNECT: return;
      case WS_EVT_DISCONNECT: return;
      case WS_EVT_PONG: return;
      case WS_EVT_ERROR: return;

      // Handle data frames.
      case WS_EVT_DATA: {
        AwsFrameInfo * info = static_cast<AwsFrameInfo *>(arg);
        // Ignore multi-frame messages for now. If we eventually need to handle them, then
        // we need to concatenate data from multiple messages until the `final` bit is set.
        if(not info->final or info->index != 0 or info->len != len) return;

        // Ignore 0 length messages.
        if (not len) return;

        // Get the code for the request.
        const uint8_t code = data[0];

        size_t offset = 1;

        switch (code) {
          // Client requested the networks that are in range. Schedule a wifi scan and
          // eventually send results back.
          case SCAN_NETWORKS: {
            if (clients_waiting_networks) {
              const ws_client_id id = client->id();
              xQueueSend(clients_waiting_networks, &id, 0);
            }
            return;
          }

          case CONNECT_NETWORK: {
            if (len < offset + 1) return;
            connect_to_router = data[offset+0];
            offset += 1;

            char * ssid = connect_to_router ? router_ssid : ap_ssid;
            char * pass = connect_to_router ? router_password : ap_password;

            if (len < offset + 1) return;
            size_t ssid_length = data[offset];
            offset += 1;

            if (len < offset + ssid_length) return;
            strncpy(ssid, reinterpret_cast<char *>(data + offset), ssid_length);
            offset += ssid_length;
            ssid[ssid_length] = '\0';

            if (len < offset + 1) return;
            size_t pass_length = data[offset];
            offset += 1;

            if (len < offset + pass_length) return;
            strncpy(pass, reinterpret_cast<char *>(data + offset), pass_length);
            offset += pass_length;
            pass[pass_length] = '\0';

            new_settings = true;
            ip = IPAddress();
            ok = false;

            // Save new wifi config when we manage to connect.

            return;
          }

          case REQUEST_STATE_UPDATES: {
            if (clients_waiting_state) {
              const ws_client_id id = client->id();
              xQueueSend(clients_waiting_state, &id, 0);
            }
            return;
          }

          case COMMAND: {
            // Command is power and seek for all 24 channels. Use 0.0 for no power and -1.0 for no seeking.
            if (len != offset + 8*24) return;

            // Check that current command is not blocked by another client.
            const unsigned long command_time = millis();
            const IPAddress ip = client->remoteIP();
            const bool holding = (command_time - last_command_time) < max_command_time;
            const bool same_client = last_command_ip == ip;

            // Ignore if a different client is holding control.
            if ((last_command_ip != default_ip) and holding and not same_client) return;

            using state::state;

            // All good, set power levels.
            for (size_t i = 0; i < 24; i++) {
              auto & channel = state.channels[i];
              using mystd::clamp;

              // Clamp power to valid range.
              channel.power_offset = clamp(get_float32(data + offset), -1.0, +1.0);

              // Clamp seek to valid range, with special case for -1 meaning no seek.
              const float seek = get_float32(data + offset + 4);
              if (seek == -1.0) channel.seek = -1.0;
              else channel.seek = clamp(seek, channel.min_position, channel.max_position);

              offset += 8;
            }

            last_command_ip = ip;
            last_command_time = command_time;

            return;
          }

          case CONFIGURE: {
            // Similarly to sending configuration, with 1 extra byte to tell if we should save it.
            if (len != 1138) return;

            const bool save = get_bool(data + offset);
            offset += 1;

            power::min_battery_voltage = get_float32(data + offset);
            offset += 4;

            using state::state;

            state.current_fraction = get_float32(data + offset);
            offset += 4;

            // Get channel config.
            for (size_t i = 0; i < 24; i++){
              state.channels[i].min_position = get_float32(data + offset + 0);
              state.channels[i].max_position = get_float32(data + offset + 4);
              state.channels[i].reverse_output = get_bool(data + offset + 8);
              state.channels[i].reverse_input = get_bool(data + offset + 9);
              state.channels[i].pid.p = get_float32(data + offset + 10);
              state.channels[i].pid.i_time = get_float32(data + offset + 14);
              state.channels[i].pid.d_time = get_float32(data + offset + 18);
              state.channels[i].pid.threshold = get_float32(data + offset + 22);
              state.channels[i].pid.overshoot_threshold = get_float32(data + offset + 26);
              state.channels[i].min_power = get_float32(data + offset + 30);
              state.channels[i].max_current = get_float32(data + offset + 34);
              state.channels[i].max_avg_current = get_float32(data + offset + 38);
              state.channels[i].enabled = get_bool(data + offset + 42);
              offset += 43;
            }

            // Get strain gauge coefficients.
            for (size_t i = 0; i < 12; i++){
              state.gauges[i].zero_offset = get_float32(data + offset + 0);
              state.gauges[i].coefficient = get_float32(data + offset + 4);
              offset += 8;
            }

            // Memory saving uses a mutex and is slow, do it in the main loop.
            if (save) save_config = true;

            return;
          }

          case RELOAD_CONFIGURATION:
            reload_config = true;
            // Fallthrough to also ask for new config.

          case REQUEST_CONFIGURATION: {
            if (clients_waiting_config) {
              const ws_client_id id = client->id();
              xQueueSend(clients_waiting_config, &id, 0);
            }
            return;
          }

          default: return;
        }

      }
      default: return;
    }
  }


  void connect_wifi(){

    // Connect to an existing wifi network.
    if (connect_to_router) {
      connected_to_router = true;

      WiFi.begin(router_ssid, router_password);
      const auto status = WiFi.waitForConnectResult();
      if (status == WL_CONNECTED) {
        ip = WiFi.localIP();
        ok = true;
        return;
      }
    }

    status = "Starting AP...";

    // Start as wifi access point if we can't connect to known network.
    connected_to_router = false;
    if(WiFi.softAP(ap_ssid, ap_password)) {
      ip = WiFi.softAPIP();
      ok = true;
    } else {
      status = "Can't start AP!";
    }
  }

  // Scan networks if request by a client.
  void send_network_scan(){
    // Invalid queue, nothing to do.
    if (not clients_waiting_networks) return;

    // Scan for nearby networks only if any client wants to know.
    if (not uxQueueMessagesWaiting(clients_waiting_networks)) return;


    // Scanning takes a while, and the first round will definitely not be complete.
    // Check number of wifi stations in range, or get scan status.
    auto n = WiFi.scanComplete();

    // Check if a scan was started or failed (failed is also the flag for no scan started).
    if (n == WIFI_SCAN_RUNNING) return;
    if (n == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(/* async = */ true);
      return;
    }
    // Scan is complete and we see `n` networks.

    // Networks message.
    String networks;

    // Networks response code.
    networks += static_cast<char>(AVAILABLE_NETOWRKS);

    // Truncate n to 1 byte, and specify how many networks we'll list.
    if (n > 0xFF) n = 0xFF;
    networks += static_cast<char>(n);

    // For each network, send over SSID length, SSID, and RSSI (signal strength).
    for (size_t i = 0; i < n; i++){
      auto const& ssid = WiFi.SSID(i);
      networks += static_cast<char>(ssid.length());
      networks += ssid;
      // The arduino wifi lib returns an int32, but the underlying ESP32 lib uses
      // int8. So first cast into int8 to keep the sign, then cast to char to append.
      networks += static_cast<char>(static_cast<int8_t>(WiFi.RSSI(i)));
    }

    // Send the network information to all waiting clients.
    ws_client_id id = 0;
    while(xQueueReceive(clients_waiting_networks, &id, 0)) {
      send_binary(id, networks.c_str(), networks.length());
    }

    // Remove scan information now that we sent it over.
    WiFi.scanDelete();
  }

  // Send state measurements.
  void send_state(){
    // Invalid queue, nothing to do.
    if (not clients_waiting_state) return;

    // Register for state if any new client requests.
    unsigned long time = millis();
    ws_client_id id = 0;
    while(xQueueReceive(clients_waiting_state, &id, 0)) {
      state_register_time[id] = time;
    }

    // Remove old clients from registry.
    for (auto it = state_register_time.begin(); it != state_register_time.end();/* increment in loop */) {
      unsigned long register_time = it->second;
      if (time - register_time > register_duration) it = state_register_time.erase(it);
      else ++it;
    }

    // Skip if no registered clients left.
    if (state_register_time.empty()) return;


    // Serialise State
    // ---------------

    // Build state message.
    const size_t state_size = 457;
    uint8_t state_msg[state_size] = {};

    using state::state;

    // State message API code.
    state_msg[0] = STATE;
    // Send 6*4 bytes of power and timing info.
    set_float32(state_msg + 1, state.voltage);
    set_float32(state_msg + 5, state.current);
    set_float32(state_msg + 9, state.power);
    set_float32(state_msg + 13, state.fps);
    set_float32(state_msg + 17, state.max_loop_duration);
    set_uint32(state_msg + 21, state.update_time);
    // For each drive channel, send 4*4 bytes of `position, current, power, seek`.
    for (size_t i = 0; i < 24; i++){
      set_float32(state_msg + 25 + i * 16, state.channels[i].position);
      set_float32(state_msg + 29 + i * 16, state.channels[i].current);
      set_float32(state_msg + 33 + i * 16, state.channels[i].power);
      set_float32(state_msg + 37 + i * 16, state.channels[i].seek);
    }
    // For each pressure channel, send 1*4 bytes of `strain`.
    for (size_t i = 0; i < 12; i++){
      set_float32(state_msg + 409 + i * 4, state.gauges[i].strain);
    }

    // Send to all registered clients.
    for (auto const& client_and_time : state_register_time) {
      send_binary(client_and_time.first, state_msg, state_size);
    }
  }

  void send_configuration(){
    // Nothing waiting, nothing to do.
    if (not clients_waiting_config) return;
    if (not uxQueueMessagesWaiting(clients_waiting_config)) return;


    // Serialise Config
    // ----------------

    // Build state message.
    const size_t config_size = 1137;
    uint8_t config_msg[config_size] = {};

    using state::state;

    // State message API code.
    config_msg[0] = CONFIGURATION;
    size_t offset = 1;

    set_float32(config_msg + offset, power::min_battery_voltage);
    offset += 4;

    set_float32(config_msg + offset, state.current_fraction);
    offset += 4;

    // Send config of each drive channel.
    for (size_t i = 0; i < 24; i++){
      set_float32(config_msg + offset + 0, state.channels[i].min_position);
      set_float32(config_msg + offset + 4, state.channels[i].max_position);
      set_bool(config_msg + offset + 8, state.channels[i].reverse_output);
      set_bool(config_msg + offset + 9, state.channels[i].reverse_input);
      set_float32(config_msg + offset + 10, state.channels[i].pid.p);
      set_float32(config_msg + offset + 14, state.channels[i].pid.i_time);
      set_float32(config_msg + offset + 18, state.channels[i].pid.d_time);
      set_float32(config_msg + offset + 22, state.channels[i].pid.threshold);
      set_float32(config_msg + offset + 26, state.channels[i].pid.overshoot_threshold);
      set_float32(config_msg + offset + 30, state.channels[i].min_power);
      set_float32(config_msg + offset + 34, state.channels[i].max_current);
      set_float32(config_msg + offset + 38, state.channels[i].max_avg_current);
      set_bool(config_msg + offset + 42, state.channels[i].enabled);
      offset += 43;
    }

    // Send strain gauge coefficients.
    for (size_t i = 0; i < 12; i++){
      set_float32(config_msg + offset + 0, state.gauges[i].zero_offset);
      set_float32(config_msg + offset + 4, state.gauges[i].coefficient);
      offset += 8;
    }

    // Make sure we filled the buffer exactly.
    assert(offset == config_size);

    // Send to all registered clients.
    ws_client_id id = 0;
    while(xQueueReceive(clients_waiting_config, &id, 0)) {
      send_binary(id, config_msg, config_size);
    }
  }




  void save_wifi_settings() {
    using namespace memory;

    set_str("router_ssid", router_ssid);
    set_str("router_pass", router_password);
    set_str("ap_ssid", ap_ssid);
    set_str("ap_pass", ap_password);
    set_bool("conn_router", connect_to_router);

    commit();
  }



  void load_wifi_settings() {
    using namespace memory;

    get_str("router_ssid", router_ssid, max_length);
    get_str("router_pass", router_password, max_length);
    get_str("ap_ssid", ap_ssid, max_length);
    get_str("ap_pass", ap_password, max_length);
    get_bool("conn_router", connect_to_router);
  }



  // We want to run some setup and web server loop in core 0.
  void setup_on_web_core(void * arg);

  void setup(){

    // Mount SPIFFS but don't format if it fails (default behaviour).
    if (not SPIFFS.begin()) return;

    // Web pages
    // ---------

    // The driver should be controllable from other sources, thus allow CORS.
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    // Send index page to the default GET request.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request){
      request->send(SPIFFS, "/index.html", "text/html");
    });

    server.on("/bundle.js", HTTP_GET, [](AsyncWebServerRequest * request){
      request->send(SPIFFS, "/bundle.js", "text/javascript");
    });


    // WebSocket
    // ---------

    // Initialize queue for all clients waiting on available networks.
    clients_waiting_networks = xQueueCreate(10, sizeof(ws_client_id));
    // Queue for waiting state.
    clients_waiting_state = xQueueCreate(10, sizeof(ws_client_id));
    // Queue for config.
    clients_waiting_config = xQueueCreate(10, sizeof(ws_client_id));
    // Add handler and initialize websocket at path `/ws`.
    ws.onEvent(on_ws_event);
    server.addHandler(&ws);


    // Default page
    // ------------

    // Make sure to respond to OPTIONS requests with the CORS header.
    server.onNotFound([](AsyncWebServerRequest * request) {
      if (request->method() == HTTP_OPTIONS) {
        request->send(200);
      } else {
        request->send(404);
      }
    });


    // Start
    // -----

    // Start the wifi update loop. We can't use delay in the server callbacks, so
    // we need to manually schedule tasks (like wifi scanning).
    xTaskCreatePinnedToCore(
      setup_on_web_core, // Function to run.
      "wifi_loop", // Name.
      16384, // Stack size in words.
      nullptr,  // Task args.
      0, // Priority.
      nullptr, // Task handle.
      0); // Core on which task runs.

    ok = true;
  }

  // Update that runs only once a few seconds.
  const auto slow_update = timing::throttle_function([](){
    // > Browsers sometimes do not correctly close the websocket connection, even
    // > when the close() function is called in javascript. This will eventually
    // > exhaust the web server's resources and will cause the server to crash.
    // > Periodically calling the cleanClients() function from the main loop()
    // > function limits the number of clients by closing the oldest client when
    // > the maximum number of clients has been exceeded. This can called be every
    // > cycle, however, if you wish to use less power, then calling as infrequently
    // > as once per second is sufficient.
    ws.cleanupClients();
  }, /* throttle_period (millis) = */ 1000);


  // Update loop running as fast as the wifi can send stuff.
  void update() {
    // Time and throttle updates.
    timer.update(min_web_update_period); // delay such that we update only every 10ms.
    // Slow update only runs sometimes.
    slow_update();


    // Reconnect if needed.
    if (new_settings) {
      save_wifi_settings();
      ESP.restart();
    }


    // Send available networks if any clients requrested them.
    send_network_scan();

    // Send state to registered clients.
    send_state();

    // Reset commands if no client is holding control.
    update_commands();


    // Reload configuration if needed.
    if (reload_config) {
      state::load_state_params();
      power::load_power_limits();
      reload_config = false;
      save_config = false;
    }
    // Send configuration info.
    send_configuration();
    // Save configuration if needed.
    if (save_config) {
      state::save_state_params();
      power::save_power_limits();
      save_config = false;
    }
  }

  // Function to run in the core dedicated to the web server.
  void setup_on_web_core(void * arg){
    // Wait for wifi connection.
    connect_wifi();
    if (ok){
      // Start HTML and WebSocket server.
      // Note internal event loop from AsyncTCP needs to be configured for core 0 too.
      server.begin();
      // Start the update loop.
      while(true) update();
    }
    // Don't start server and managing loop if wifi not enabled. The failure should be
    // Marked on screen from the ui.
  }

}