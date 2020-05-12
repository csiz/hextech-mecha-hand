// Driver interface
// ----------------

/* Version of the driver library. */
export const version = 1;

/* Connect to a motor driver and continuously request state updates. */
export class MotorDriver {
  /* Connect to driver at url. */
  constructor(url = null) {

    /* Callback invoked when a new driver state is received. */
    this.onstate = () => {};
    /* Callback invoked when the driver configuration is received. */
    this.onconfig = () => {};
    /* Callback invoked when receiving a list of wifi networks visible to the motor driver. */
    this.onnetworks = () => {};

    /* Callback invoked right before new commands are sent. */
    this.onsendcommands = () => {};

    /* Callback invoked when connecting. */
    this.onconnecting = () => {};
    /* Callback invoked when connection is succesful and we begin requesting state. */
    this.onconnected = () => {};
    /* Callback invoked on disconnects and disable. */
    this.onclose = () => {};

    /* Commands to send to the motor driver; see `command` and `release`. */
    this.commands = zero_commands();

    /* Latest state of the motor driver. */
    this.state = null;

    /* List containing recent states, with the most recent first. */
    this.state_history = [];

    /* How much state history to keep. */
    this.max_history_entries = 200; // About 10 seconds at 50Hz updates.

    /* Motor driver configuration. */
    this.config = null;

    /* WiFi networks visible form the motor driver; list of {ssid, rssi}. */
    this.networks = [];

    /* Continuously reconnect to the motor driver until disabled by the `close` method. */
    this.disabled = false;

    /* URL of the motor driver's websocket. */
    this.url = url;

    /* Websocket connection, managed by connect and close. */
    this.socket = null;

    /* Time (ms) after the last update when we attempt reconnecting. */
    this.no_update_timeout = 2000;

    /* Time (ms) when connection was initiated. */
    this.connect_initiated_time = null;

    /* Handle for the repeated update requests. */
    this.request_state_updates_handle = null;

    /* Handle for the repeated commands. */
    this.send_commands_handle = null;
  }

  /* Continuously command the motors. */
  command() {
    if (this.send_commands_handle != null) return;
    this.send_commands_handle = setInterval(() => {this.send_commands()}, 50);
  }

  /* Release command of the motors. */
  release() {
    if (this.send_commands_handle != null) {
      // Stop sending commands; if scheduled.
      clearInterval(this.send_commands_handle);
      this.send_commands_handle = null;

      // Send the null command to stop motors.
      this.send_commands(zero_commands());
    }
  }

  /* Stop requesting state updates. */
  stop_updates() {
    if (this.request_state_updates_handle != null) {
      clearInterval(this.request_state_updates_handle);
      this.request_state_updates_handle = null;
    }
  }

  /* Send messages only if socket is ready. */
  send(message){
    if (this.socket != null && this.socket.readyState == WebSocket.OPEN) this.socket.send(message);
  }

  /* Close driver connection and disable further reconnects. */
  close(){
    this.disabled = disable;
    if (this.socket != null) this.socket.close(1000);
  }

  /* Check if we received updates and reconnect. */
  check_connection() {
    // Skip teh check if we didn't receive any updates yet. Let the websocket handle it.
    if (this.state == null) return;

    // Reconnct if the last update is stale.
    if (Date.now() > this.state.local_time + this.no_update_timeout) {
      console.info("Stale connection; attempting reconnect.");
      this.reconnect();
    }
  }

  /* Connect to the motor drive and begin requesting state updates. */
  connect(url = null) {
    if (url != null) this.url = url;

    if (this.url == null) throw Error("No URL provided!");

    this.connect_initiated_time = Date.now();

    let new_socket = new WebSocket(this.url);

    this.onconnecting();

    new_socket.onopen = (event) => {
      // Update socket with the newly opened one.
      this.socket = new_socket;

      this.scan_networks();
      this.request_config();

      // Keep asking for state updates. The cutoff on the chip side is 200ms, so
      // if we ask every 80ms we should always recive updates. The `send` function
      // guards against sending to an uninitialzed/unconnected socket.
      this.request_state_updates_handle = setInterval(
        () => {
          this.request_state_updates();
          // Use the update loop to also ensure we're still connected.
          this.check_connection();
        },
        80);


      this.onconnected();
    };

    new_socket.onclose = (event) => {
      if (event.code != 1000) console.error("Socket closed abnormally.");

      // Disable socket.
      this.socket = null;

      this.onclose();

      this.stop_updates();

      // Immediately attempt to reconnect on the next event loop.
      if (!this.disabled) setTimeout(() => this.connect(), 0);
    };

    new_socket.onerror = (err) => {
      console.error("Socket error.");
      this.reconnect();
    };

    new_socket.onmessage = async (event) => {
      let buffer;
      if (event.data instanceof Blob) {
        buffer = await event.data.arrayBuffer();
      } else if (event.data instanceof ArrayBuffer) {
        buffer = event.data;
      } else {
        console.error("Received data is not binary!");
        return;
      }

      // All messages beging with a code byte followed by data.
      let code = new DataView(buffer, 0, 1).getUint8(0);
      let data = new DataView(buffer, 1);

      switch (code) {
        case AVAILABLE_NETOWRKS:
          this.receive_networks(data);
          return;
        case STATE:
          this.receive_state(data);
          return;
        case CONFIGURATION:
          this.receive_config(data);
          return;
        default:
          console.warn(`Unknown code: ${code}`);
          return;
      }
    };
  }

  /* Reconnect, ignoring previous socket. */
  reconnect() {
    // To reconnect fast without waiting for the close handshake, we need to ignore
    // the already opened socket and move straight to opening a new connection.

    // Stop automatic transmissions.
    this.stop_updates();
    this.release();

    // Clear out existing handlers.
    if (this.socket != null) {
      this.socket.onopen = () => {};
      this.socket.onclose = () => {};
      this.socket.onerror = () => {};
      this.socket.onmessage = () => {};

      this.socket = null;
    }

    // Reset the state.
    this.state = null;

    // Immediately start a new connection.
    this.connect();
  }

  /* Request the driver to scan for visible wifi access points. */
  scan_networks() {
    let data = new Uint8Array(1);
    data[0] = SCAN_NETWORKS;
    this.send(data.buffer);
  }


  /* Instruct driver to connect to wifi router, or create an access point. */
  connect_to_network(ssid, pass, connect_to_router) {
    // We need to encode text fields as utf-8 bytes.
    let encoder = new TextEncoder();
    let ssid_bytes = encoder.encode(ssid, "utf-8");
    let pass_bytes = encoder.encode(pass, "utf-8");

    let data = new Uint8Array(4 + ssid_bytes.length + pass_bytes.length);
    let offset = 0;

    // Set the function code.
    data[offset] = CONNECT_NETWORK;
    offset += 1;

    // Set whether it's an external router or internal access point.
    data[offset] = connect_to_router ? 1 : 0;
    offset += 1;

    // Send ssid and password as length byte and text bytes.
    data[offset] = ssid_bytes.length;
    offset += 1;
    data.set(ssid_bytes, offset);
    offset += ssid_bytes.length;

    data[offset] = pass_bytes.length;
    offset += 1;
    data.set(pass_bytes, offset);
    offset += pass_bytes.length;

    // Send payload.
    this.send(data.buffer);
  }

  /* Receive wifi networks visible by the motor driver. */
  receive_networks(data){
    // Read number of networks.
    let n = data.getUint8(0);
    // Interpret each network.
    let offset = 1;
    // Decode the names using UTF-8.
    let decoder = new TextDecoder("utf-8");

    let new_networks = [];
    for (let i = 0; i < n; i++) {
      // Read SSID length.
      let length = data.getUint8(offset);
      offset += 1;
      // Read SSID.
      let ssid = decoder.decode(new DataView(data.buffer, data.byteOffset + offset, length));
      offset += length;
      // Read RSSI (wifi strength).
      let rssi = data.getInt8(offset);
      offset += 1;

      new_networks.push({ssid, rssi});
    }

    // Update with scanned networks.
    this.networks = new_networks;

    // Invoke callback for the networks list.
    this.onnetworks(this.networks);
  }


  /* Request state updates from the driver, request must be repeated to keep connection alive. */
  request_state_updates(){
    let data = new Uint8Array(1);
    data[0] = REQUEST_STATE_UPDATES;
    this.send(data.buffer);
  }

  /* Receive latest motor driver state. */
  receive_state(data){
    // Ignore if we didn't get the complete message.
    if (data.byteLength != 457 - 1) return;

    let offset = 0;

    const voltage = data.getFloat32(offset);
    offset += 4;
    const current = data.getFloat32(offset);
    offset += 4;
    const power = data.getFloat32(offset);
    offset += 4;
    const fps = data.getFloat32(offset);
    offset += 4;
    const max_loop_time = data.getFloat32(offset);
    offset += 4;
    const driver_time = data.getUint32(offset);

    // Assume 100ms elapses on the first update.
    const driver_elapsed = this.state == null ? 100 : uint32_time_interval(this.state.driver_time, driver_time);
    offset += 4;

    const local_time = Date.now();
    const local_elapsed = this.state == null ? 100 : (local_time - this.state.local_time);

    let motor_channels = [];
    for (let i = 0; i < 24; i++) {
      const position = data.getFloat32(offset);
      offset += 4;
      const current = data.getFloat32(offset);
      offset += 4;
      const power = data.getFloat32(offset);
      offset += 4;
      const seek = data.getFloat32(offset);
      offset += 4;

      motor_channels.push({position, current, power, seek});
    }


    let pressure_channels = [];
    for (let i = 0; i < 12; i++) {
      const strain = data.getFloat32(offset);
      offset += 4;

      pressure_channels.push({strain});
    }

    this.state = {
      voltage,
      current,
      power,
      fps,
      max_loop_time,
      driver_time,
      driver_elapsed,
      local_time,
      local_elapsed,

      motor_channels,
      pressure_channels,
    };

    // Add the recent state to the front of history. Use unshift until we actually
    // have perfomance issues. We also keep the array small, so it should be fine.
    this.state_history.unshift(this.state);

    // Discard old elements from history until we satisfy the max entries condition.
    // We should only ever discard one per update, but use a while statement anyway.
    while (this.state_history.length > this.max_history_entries) this.state_history.pop();

    // Invoke the callback for a state update.
    this.onstate(this.state);
  }


  /* Request the current configuration of the motor driver. */
  request_config(){
    let data = new Uint8Array(1);
    data[0] = REQUEST_CONFIGURATION;
    this.send(data.buffer);
  }

  /* Have the driver reload configuration from internal memory. */
  reload_config() {
    let data = new Uint8Array(1);
    data[0] = RELOAD_CONFIGURATION;
    this.send(data.buffer);
  }

  /* Receive current driver configuration. */
  receive_config(data){
    // Ignore if we didn't get the complete message.
    if (data.byteLength != 817 - 1) return;

    let offset = 0;

    let motor_channels = [];
    for (let i = 0; i < 24; i++) {
      const min_position = data.getFloat32(offset + 0);
      const max_position = data.getFloat32(offset + 4);
      const reverse_output = Boolean(data.getUint8(offset + 8));
      const reverse_input = Boolean(data.getUint8(offset + 9));
      const p = data.getFloat32(offset + 10);
      const i_time = data.getFloat32(offset + 14);
      const d_time = data.getFloat32(offset + 18);
      const threshold = data.getFloat32(offset + 22);
      const overshoot_threshold = data.getFloat32(offset + 26);

      offset += 30;

      motor_channels.push({
        min_position,
        max_position,
        reverse_output,
        reverse_input,
        p,
        i_time,
        d_time,
        threshold,
        overshoot_threshold,
      });
    }

    let pressure_channels = [];
    for (let i = 0; i < 12; i++) {
      const zero_offset = data.getFloat32(offset + 0);
      const coefficient = data.getFloat32(offset + 4)
      offset += 8;

      pressure_channels.push({zero_offset, coefficient});
    }

    this.config = {
      motor_channels,
      pressure_channels,
    };

    // Invoke callback when new configuration received.
    this.onconfig(this.config);
  }

  /* Send motor commands, assuming correctly formated values. */
  send_commands(commands = null) {
    // Prepare commands if anyone's listening.
    this.onsendcommands();

    if (commands == null) commands = this.commands;

    // We need the current configuration to determine proper seek values.
    if (this.config == null) return;

    let {power, seek} = commands;

    if (power.length != 24 || seek.length != 24) {
      throw new Error("Commands must be set for exactly 24 channels!");
    }

    let clamped_power = power.map(p => clamp(p, -1.0, +1.0));
    let adjusted_seek = seek.map((s, i) => {
      if (s == null || s == -1) return -1;
      return interpolate(
        clamp(s, 0.0, 1.0),
        this.config.motor_channels[i].min_position,
        this.config.motor_channels[i].max_position);
    });

    // Build response and send it via websockets.
    let data = new Uint8Array(1 + 8*24);
    let data_view = new DataView(data.buffer);
    let offset = 0;

    // Set the function code.
    data[offset] = COMMAND;
    offset += 1;

    for (let i = 0; i < 24; i++){
      data_view.setFloat32(offset, clamped_power[i]);
      data_view.setFloat32(offset+4, adjusted_seek[i]);
      offset += 8;
    }

    // Send payload.
    this.send(data.buffer);
  }

  /* Send new configuration, optionally saving it on the driver's non-volatile memory. */
  send_config(new_config, save = false){
    // TODO: configure strain gauges too.

    // Build response and send it via websockets.
    let data = new Uint8Array(818);
    let data_view = new DataView(data.buffer);

    // Set the function code.
    data[0] = CONFIGURE;
    let offset = 1;

    // Set save flag.
    data_view.setUint8(offset, save);
    offset += 1;

    // Set channel configuration.
    let channels = new_config.motor_channels;
    for (let i = 0; i < 24; i++){
      data_view.setFloat32(offset + 0, channels[i].min_position);
      data_view.setFloat32(offset + 4, channels[i].max_position);
      data_view.setUint8(offset + 8, channels[i].reverse_output);
      data_view.setUint8(offset + 9, channels[i].reverse_input);
      data_view.setFloat32(offset + 10, channels[i].p);
      data_view.setFloat32(offset + 14, channels[i].i_time);
      data_view.setFloat32(offset + 18, channels[i].d_time);
      data_view.setFloat32(offset + 22, channels[i].threshold);
      data_view.setFloat32(offset + 26, channels[i].overshoot_threshold);
      offset += 30;
    }
    // Set strain gauge configuration.
    let pressures = new_config.pressure_channels;
    for (let i = 0; i < 12; i++) {
      data_view.setFloat32(offset + 0, pressures[i].zero_offset);
      data_view.setFloat32(offset + 4, pressures[i].coefficient);
      offset += 8;
    }

    // Send payload.
    this.send(data.buffer);
  }
}


// Remote function opcodes
// -----------------------

const STATE = 0x00; // Get measurements, power.
const COMMAND = 0x01; // Command to position, or drive power.
const CONFIGURATION = 0x02; // Get configuration (max limits, direction, PID params).
const CONFIGURE = 0x03; // Set configuration.
const SCAN_NETWORKS = 0x04; // Scan for wifi networks in range.
const AVAILABLE_NETOWRKS = 0x05; // Reply with networks in range.
const CONNECT_NETWORK = 0x06; // Connect to network or start access point.
const REQUEST_STATE_UPDATES = 0x07; // Register for state updates.
const REQUEST_CONFIGURATION = 0x08; // Ask for configuration.
const RELOAD_CONFIGURATION = 0x09; // Reload and ask for configuration.


// Utils
// -----

export function zero_commands(){
  return {
    power: new Array(24).fill(0.0),
    seek: new Array(24).fill(null),
  };
}

/* Compute time interval from u32 time a to u32 time b.

The motor drivers uses unsigned 32 bit integers to represent time. To get uint32
arithmetic correctly in javascript we need to cast our numbers into a typed array.
*/
export function uint32_time_interval(a, b) {
  let v = new Uint32Array(3);
  v[0] = a;
  v[1] = b;
  v[2] = b - a;
  return v[2];
};

/* Clamp number between min and max. */
export function clamp(num, min, max) {
  return num <= min ? min : num >= max ? max : num;
}

/* 1 step of an exponential average with coefficient gamma. */
export function exp_average(value, last_value, gamma){
  return value * gamma + last_value * (1-gamma);
}

/* Interpolate fraction between min and max value. */
export function interpolate(f, min, max){
  return f * max + (1 - f) * min;
}

/* Get fraction between min and max value. */
export function deinterpolate(p, min, max){
  return (p - min) / (max - min);
}
