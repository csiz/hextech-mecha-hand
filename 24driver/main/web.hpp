#pragma once


#include "freertos/queue.h"
#include "WiFi.h"
#include "SPIFFS.h"
#include "ESPAsyncWebServer.h"
#include "AsyncWebSocket.h"

#include <string.h>

namespace web {
  // WiFi Bug
  // --------
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

    STATE = 0x00, // Get measurements, power.
    COMMAND = 0x01, // Command to position, or drive power.
    CONFIGURATION = 0x02, // Get configuration (max limits, direction, PID params).
    CONFIGURE = 0x03, // Set configuration.
    SCAN_NETWORKS = 0x04, // Scan for wifi networks in range.
    AVAILABLE_NETOWRKS = 0x05, // Reply with networks in range.
    CONNECT_NETWORK = 0x06, // Connect to network or start access point.
  };

  const size_t MAX_LENGTH = 256;
  // Default SSID and password when creating an Access Point.
  char ap_ssid[MAX_LENGTH] = "ESP32-24Driver";
  char ap_password[MAX_LENGTH] = "give me a hand";
  // SSID and password when connecting to an existing network.
  char router_ssid[MAX_LENGTH] = "";
  char router_password[MAX_LENGTH] = "";
  // Whether to connect to a router or start an AP; always fallback on AP.
  bool connect_to_router = false;
  // Whether we actually conencted to router or AP.
  bool connected_to_router = false;
  // Whether new connection needs to be established, and new settings saved.
  bool new_settings = false;
  // Whether we need to save settings. Do this by setting a flag so we don't
  // depend on memory from this file. Only memory depends on web.
  bool save_settings = false;

  // Workaround to switching from AP to STA mode. We'll do it by restarting the chip.
  bool needs_restart = false;

  // Server port to use, 80 for HTTP/WS or 443 for HTTPS/WSS.
  const int PORT = 80;

  // IP address to get to the server.
  IPAddress ip = {};

  // TODO: also have some form of error reporting and error check.
  // Whether wifi and everything was initialized without error.
  bool ok = false;

  // Web servers, and web socket handler.
  AsyncWebServer server(PORT);
  AsyncWebSocket ws("/ws");


  // Replying with available networks takes a lot of time to finish scanning.
  // We'll schedule scans on the update loop, and reply to all clients in queue.
  typedef uint32_t ws_client_id;
  QueueHandle_t clients_waiting_networks = {};


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

    // Start as wifi access point if we can't connect to known network.
    connected_to_router = false;
    if(WiFi.softAP(ap_ssid, ap_password)) {
      ip = WiFi.softAPIP();
      ok = true;

    } else {
      // TODO: report we can't start an access point.
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
      ws.binary(id, networks.c_str(), networks.length());
    }

    // Remove scan information now that we sent it over.
    WiFi.scanDelete();
  }


  // We want to run the wifi management loop in core 0.
  void update_loop(void * arg);

  void setup(){
    // Mount SPIFFS but don't format if it fails (default behaviour).
    if (not SPIFFS.begin()) return;

    connect_wifi();

    // Web pages
    // ---------

    // The driver should be controllable from other sources, thus allow CORS.
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    // Send index page to the default GET request.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request){
      request->send(SPIFFS, "/index.html", "text/html");
    });

    server.on("/d3-selection.min.js", HTTP_GET, [](AsyncWebServerRequest * request){
      request->send(SPIFFS, "/d3-selection.min.js", "text/javascript");
    });


    // WebSocket
    // ---------

    // Initialize queue for all clients waiting on available networks.
    clients_waiting_networks = xQueueCreate(10, sizeof(ws_client_id));
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

    // Start HTML and WebSocket server, hopefully configured for core 0.
    server.begin();


    // Start the wifi update loop. We can't use delay in the server callbacks, so
    // we need to manually schedule tasks (like wifi scanning).
    xTaskCreatePinnedToCore(
      update_loop, // Function to run.
      "wifi_loop", // Name.
      10000, // Stack size in words.
      nullptr,  // Task args.
      0, // Priority.
      nullptr, // Task handle.
      0); // Core on which task runs.

    ok = true;
  }

  void update() {
    delay(1000);

    // > Browsers sometimes do not correctly close the websocket connection, even
    // > when the close() function is called in javascript. This will eventually
    // > exhaust the web server's resources and will cause the server to crash.
    // > Periodically calling the cleanClients() function from the main loop()
    // > function limits the number of clients by closing the oldest client when
    // > the maximum number of clients has been exceeded. This can called be every
    // > cycle, however, if you wish to use less power, then calling as infrequently
    // > as once per second is sufficient.
    ws.cleanupClients();

    // Reconnect if needed.
    if (new_settings) {
      new_settings = false;
      save_settings = true;
      needs_restart = true;
    }

    if (needs_restart and not save_settings) {
      ESP.restart();
    }


    // Send available networks if any clients requrested them.
    send_network_scan();
  }

  void update_loop(void * arg){
    while(true) update();
  }

}