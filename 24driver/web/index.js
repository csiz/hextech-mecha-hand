"use strict";

import {select, selectAll} from "d3-selection";
import {range} from "d3-array";
import {line} from "d3-shape";
import {scaleLinear} from "d3-scale";

let d3 = {select, selectAll, range, line, scaleLinear};

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

// Websocket url.
const url = "ws://" + location.host + "/ws";


// Websocket, set by connect().
let socket = null;

// Send messages only if socket is ready.
function send(message){
  if (socket != null && socket.readyState == socket.OPEN) socket.send(message);
}

// Close socket if not socket not already closed.
function close(){
  if (socket != null && socket.readyState == socket.CLOSE) socket.close();
}


function scan_networks() {
  let data = new Uint8Array(1);
  data[0] = SCAN_NETWORKS;
  send(data.buffer);
}

function request_state_updates(){
  let data = new Uint8Array(1);
  data[0] = REQUEST_STATE_UPDATES;
  send(data.buffer);
}

function connect_to_network() {
  let router_selection = d3.select("#connection-type").select('input:checked').node().value;
  let connect_to_router = (router_selection == "router") ? true : false;
  let ssid = d3.select("#ssid").node().value;
  let pass = d3.select("#pass").node().value;

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
  data[offset] = connect_to_network ? 1 : 0;
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
  send(data.buffer);
}

// Available networks as list of {ssid, rssi}.
let networks = [];

function receive_networks(data){
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
  networks = new_networks;

  // Show in the page.
  d3.select("#available-networks")
    .selectAll("li")
    .data(networks)
    .join("li")
      .text(({ssid, rssi}) => `SSID: ${ssid} RSSI: ${rssi}dBm`);

  // And in the suggested list.
  d3.select("#networks-list")
    .selectAll("option")
    .data(networks)
    .join("option")
      .attr("value", ({ssid}) => ssid);
}

// Add functionality to the buttons.
d3.select("#scan-networks").on("click", () => scan_networks());
d3.select("#connect-network").on("click", () => connect_to_network());


let uint32_time_interval = (function (){
  // To get uint32 arithmetic correctly in javascript we need to cast
  // our numbers into a typed array. Also capture it in a lambda so
  // we hide the array from global scope.
  let v = new Uint32Array(3);
  return function(a, b) {
    v[0] = a;
    v[1] = b;
    v[2] = b - a;
    return v[2];
  };
})();

// Store up to 5 seconds of data (~ 500 data points per measurement per channel).
const max_duration = 5.0;
// Ignore first few milliseconds to avoid flickering from slightly delayed data.
const min_duration = 0.100;

// Store the state measurements from the chip.
let state = {
  voltage: 0.0,
  current: 0.0,
  power: 0.0,
  fps: 0.0,
  max_loop_time: 0.0,
  last_update_time: 0.0,
  last_update_local_time: Date.now(),
  last_connect_local_time: 0,
  local_duration_since_last_update: 0.0,
  smooth_elapsed_difference: 0.0,

  // List of all update times.
  times: [],
  // Duration from previous update to last update (ie. 4 seconds ago).
  durations_to_last: [],
  // Duration from update to now.
  durations_to_now: [],

  // A 24 length array of drive channel measurements and power setting.
  channels: d3.range(24).map((i) => ({
    position: [],
    current: [],
    power: [],
    seek: [],

    // And configuration.
    min_position: 0.0,
    max_position: 1.0,
    reverse_output: false,
    reverse_input: false,

    // Auto limits scan for the minimum position and maximum position and sets the config.
    auto_limits: false,
    auto_min_position: null,
    auto_max_position: null,
  })),

  // A 12 length array of pressure measurements.
  pressures: d3.range(12).map((i) => ({
    strain: [],

    // And configuration.
    zero_offset: 0.0,
    coefficient: 1.0,
  })),


  // Flag whether configuration was received. We shouldn't save it until we get starting values.
  initial_configuration_received: false,
};


function update_state() {
  let now = Date.now();

  state.local_duration_since_last_update = (now - state.last_update_local_time) * 0.001;

  // Recompute durations since this update; using uint32 arithmetic, and converting ms to s.
  state.durations_to_last = state.times.map(t => uint32_time_interval(t, state.last_update_time) * 0.001);
  state.durations_to_now = state.durations_to_last.map(t => t + state.local_duration_since_last_update);

  // Assume times are ordered, and therefore durations is monotonically decreasing.
  // Check how many update are no longer relevant and remove them.
  let stale = 0;
  while (// We have to check that we're not overshooting the list.
    stale < state.durations_to_now.length &&
    state.durations_to_now[stale] > (max_duration + min_duration)) stale++;

  // Cut off all stale updates.
  state.times = state.times.slice(stale);
  state.durations_to_last = state.durations_to_last.slice(stale);
  state.durations_to_now = state.durations_to_now.slice(stale);
  for (let j = 0; j < 24; j++) {
    state.channels[j].position = state.channels[j].position.slice(stale);
    state.channels[j].current = state.channels[j].current.slice(stale);
    state.channels[j].power = state.channels[j].power.slice(stale);
    state.channels[j].seek = state.channels[j].seek.slice(stale);
  }
  for (let j = 0; j < 12; j++) {
    state.pressures[j].strain = state.pressures[j].strain.slice(stale);
  }
}

function update_and_show_state(){
  update_state();
  show_state();
}

function clamp(num, min, max) {
  return num <= min ? min : num >= max ? max : num;
}

function exp_average(value, last_value, gamma){
  return value * gamma + last_value * (1-gamma);
}

function last(array) {
  return array[array.length - 1];
}

function receive_state(data){

  // Ignore if we didn't get the complete message.
  if (data.byteLength != 457 - 1) return;

  let offset = 0;

  state.voltage = data.getFloat32(offset);
  offset += 4;
  state.current = data.getFloat32(offset);
  offset += 4;
  state.power = data.getFloat32(offset);
  offset += 4;
  state.fps = data.getFloat32(offset);
  offset += 4;
  state.max_loop_time = data.getFloat32(offset);
  offset += 4;
  let update_time = data.getUint32(offset);
  let elapsed = uint32_time_interval(state.last_update_time, update_time) * 0.001;
  state.last_update_time = update_time;
  offset += 4;

  state.times.push(state.last_update_time);

  let now = Date.now();
  let elapsed_local = (now - state.last_update_local_time) * 0.001;
  state.last_update_local_time = now;


  // Adjust for network latency, see `duration_at_index` for explanation.

  // Compute smoothed elapsed time differences, but only if they're small enough.
  let diff = elapsed_local - elapsed;

  // Smoothed time difference between chip time and local time. We need this
  // to avoid graphs jumping around due to different latency between state updates.
  let smooth_dif = exp_average(
    diff,
    state.smooth_elapsed_difference,
    0.9
  );

  // Clamp the difference to at most a half second interval either side.
  state.smooth_elapsed_difference = clamp(smooth_dif, -0.500, 0.500);


  let channels = state.channels;
  for (let i = 0; i < 24; i++) {
    channels[i].position.push(data.getFloat32(offset));
    offset += 4;
    channels[i].current.push(data.getFloat32(offset));
    offset += 4;
    channels[i].power.push(data.getFloat32(offset));
    offset += 4;
    channels[i].seek.push(data.getFloat32(offset));
    offset += 4;
  }

  let pressures = state.pressures;

  for (let i = 0; i < 12; i++) {
    pressures[i].strain.push(data.getFloat32(offset));
    offset += 4;
  }

  // Update auto limits if enabled.
  for (let i = 0; i < 24; i++) {
    let channel = state.channels[i];
    if (channel.auto_limits) {
      channel.auto_min_position = Math.min(channel.auto_min_position, last(channel.position));
      channel.auto_max_position = Math.max(channel.auto_max_position, last(channel.position));
    }
  }

  update_state();
}

function request_config(){
  let data = new Uint8Array(1);
  data[0] = REQUEST_CONFIGURATION;
  send(data.buffer);
}

function reload_config() {
  let data = new Uint8Array(1);
  data[0] = RELOAD_CONFIGURATION;
  send(data.buffer);
}

function receive_config(data){
  // Ignore if we didn't get the complete message.
  if (data.byteLength != 337 - 1) return;

  let offset = 0;

  let channels = state.channels;
  for (let i = 0; i < 24; i++) {
    channels[i].min_position = data.getFloat32(offset + 0);
    channels[i].max_position = data.getFloat32(offset + 4);
    channels[i].reverse_output = Boolean(data.getUint8(offset + 8));
    channels[i].reverse_input = Boolean(data.getUint8(offset + 9));
    offset += 10;
  }

  let pressures = state.pressures;
  for (let i = 0; i < 12; i++) {
    pressures[i].zero_offset = data.getFloat32(offset + 0);
    pressures[i].coefficient = data.getFloat32(offset + 4)
    offset += 8;
  }

  state.initial_configuration_received = true;
  d3.select("#save-config").property("disabled", false);

  requestAnimationFrame(show_config);
}


const width = 300;
const height = 80;
// Hide latest few ms of state updates to avoid flickering.
const time_scale = d3.scaleLinear([max_duration, min_duration], [0.0, width]);
const position_seek_scale = d3.scaleLinear([0.0, 1.0], [height-1, 1]);
const power_scale = d3.scaleLinear([-1.0, +1.0], [height-1, 1]);
// Current is in Ampere, but usually it's very low. This might overflow.
const current_scale = d3.scaleLinear([0.0, 0.5], [height-1, 1]);

// Strain scale in Volts (TODO change to some force unit maybe Newton, after calibrating).
const pressure_scale = d3.scaleLinear([-0.01, 0.01], [height-1, 1]);


// Use the last update time as long as we're getting updates consistently.
function duration_at_index(i) {
  // Adjust time to account for spiky network latency.
  //
  // If a loop elapsed time on the esp32 is lower than update elapsed time in the browser
  // then there was extra latency on the network. We should compensate for this by adding the
  // difference. Since the difference is a crude estimate we need to smooth it out over time.
  // Note that it should always oscillate around 0s difference, which would be the case if
  // there was perfectly constant latency between esp32 updates and the browser.
  return time_scale(state.durations_to_now[i] + state.smooth_elapsed_difference);
}

let position_line = d3.line()
  .x((_, i) => duration_at_index(i))
  .y(p => position_seek_scale(p));

let seek_line = d3.line()
  .x((_, i) => duration_at_index(i))
  .y(s => position_seek_scale(s))
  .defined(s => s != -1.0);

let power_line = d3.line()
  .x((_, i) => duration_at_index(i))
  .y(p => power_scale(p));

let current_line = d3.line()
  .x((_, i) => duration_at_index(i))
  .y(c => current_scale(c));

let pressure_line = d3.line()
  .x((_, i) => duration_at_index(i))
  .y(c => pressure_scale(c));



function show_state(){
  // TODO: show power and timing info
  d3.selectAll("#drivers>div")
    .data(state.channels)
    .each(function (channel, i) {
      let div = d3.select(this);

      div.select("path.position").attr("d", position_line(channel.position));
      div.select("path.seek").attr("d", seek_line(channel.seek));
      div.select("path.power").attr("d", power_line(channel.power));
      div.select("path.current").attr("d", current_line(channel.current));

      // If we're tracking position limits, then take over the limit lines.
      if (channel.auto_limits) {
        div.select("line.min-position")
          .attr("y1", position_seek_scale(channel.auto_min_position))
          .attr("y2", position_seek_scale(channel.auto_min_position));
        div.select("line.max-position")
          .attr("y1", position_seek_scale(channel.auto_max_position))
          .attr("y2", position_seek_scale(channel.auto_max_position));
      }

    });

  d3.selectAll("#gauges>div")
    .data(state.pressures)
    .each(function (pressure, i) {
      let div = d3.select(this);

      div.select("path.pressure").attr("d", pressure_line(pressure.strain));
    });
}

function show_config(){
  d3.selectAll("#drivers>div")
    .data(state.channels)
    .each(function (channel, i) {
      let div = d3.select(this);

      div.select("input.set-min-position").property("value", channel.min_position.toFixed(2));
      div.select("input.set-max-position").property("value", channel.max_position.toFixed(2));

      div.select("line.min-position")
        .attr("y1", position_seek_scale(channel.min_position))
        .attr("y2", position_seek_scale(channel.min_position));
      div.select("line.max-position")
        .attr("y1", position_seek_scale(channel.max_position))
        .attr("y2", position_seek_scale(channel.max_position));

      div.select("input.set-reverse-input")
        .property("checked", channel.reverse_input);
      div.select("input.set-reverse-output")
        .property("checked", channel.reverse_output);
    });
}

let commands_handle = null;

function interpolate(f, min, max){
  return f * max + (1 - f) * min;
}

function send_commands() {

  // Get the power level from each slider.
  let set_power = [];
  d3.selectAll("#drivers input.set-power")
    .each(function() {
      set_power.push(this.value * 0.1 - 1.0);
    });

  // Get whether channels is actively seeking.
  let seek_active = [];
  d3.selectAll("#drivers input.set-seek-active")
    .each(function() {
      seek_active.push(this.checked);
    });

  // Get seek positions.
  let set_seek = [];
  d3.selectAll("#drivers input.set-seek")
  .each(function(_channel, i) {
    // Get [0.0, 1.0] seek position from the slider.
    let seek_position = this.value * 0.05;

    if (seek_active[i]) {
      // Send interpolated seek position if active.
      let channel = state.channels[i];
      set_seek.push(interpolate(seek_position, channel.min_position, channel.max_position));
    } else {
      // Send -1 if not actively seeking.
      set_seek.push(-1.0);
    }
  });

  // Build response and send it via websockets.
  let data = new Uint8Array(1 + 8*24);
  let data_view = new DataView(data.buffer);
  let offset = 0;

  // Set the function code.
  data[offset] = COMMAND;
  offset += 1;


  for (let i = 0; i < 24; i++){
    data_view.setFloat32(offset, set_power[i]);
    data_view.setFloat32(offset+4, set_seek[i]);
    offset += 8;
  }

  // Send payload.
  send(data.buffer);
}


function send_config(save = false){
  // Don't save if we didn't get the inital config.
  if (!state.initial_configuration_received) save = false;

  // Get the limits from the input fields.
  d3.selectAll("#drivers input.set-min-position")
    .each(function(_channel, i) {
      state.channels[i].min_position = this.value;
    });
  d3.selectAll("#drivers input.set-max-position")
    .each(function(_channel, i) {
      state.channels[i].max_position = this.value;
    });

  // Get channel reversals (whether the cables are connected the other way around).
  d3.selectAll("#drivers input.set-reverse-input")
    .each(function(_channel, i) {
      state.channels[i].reverse_input = this.checked;
    });
  d3.selectAll("#drivers input.set-reverse-output")
    .each(function(_channel, i) {
      state.channels[i].reverse_output = this.checked;
    });

  // TODO: configure strain gauges too.

  // Build response and send it via websockets.
  let data = new Uint8Array(338);
  let data_view = new DataView(data.buffer);

  // Set the function code.
  data[0] = CONFIGURE;
  let offset = 1;

  // Set save flag.
  data_view.setUint8(offset, save);
  offset += 1;

  // Set channel configuration.
  let channels = state.channels;
  for (let i = 0; i < 24; i++){
    data_view.setFloat32(offset + 0, channels[i].min_position);
    data_view.setFloat32(offset + 4, channels[i].max_position);
    data_view.setUint8(offset + 8, channels[i].reverse_output);
    data_view.setUint8(offset + 9, channels[i].reverse_input);
    offset += 10;
  }
  // Set strain gauge configuration.
  let pressures = state.pressures;
  for (let i = 0; i < 12; i++) {
    data_view.setFloat32(offset + 0, pressures[i].zero_offset);
    data_view.setFloat32(offset + 4, pressures[i].coefficient);
    offset += 8;
  }

  // Send payload.
  send(data.buffer);

  // Request updated config.
  request_config();
}

function start_command_sliders() {
  // Skip if command sliders already activated.
  if (commands_handle != null) return;
  commands_handle = setInterval(send_commands, 100);
}

function reset_command_sliders(){
  // Clear commands sliders; if activated.
  if (commands_handle != null) {
    clearInterval(commands_handle);
    commands_handle = null;
  }
  // Reset power levels to 0.
  d3.selectAll("#drivers input.set-power").each(function(){
    this.value = 10;
  });
  // Disable seeking, but note that we leave all seek positions as they are.
  d3.selectAll("#drivers input.set-seek-active").property("checked", false);

  // Send the inputs that were reset.
  send_commands();
}

function reset_command_sliders_if_not_seeking(){
  // Check if all seek checkboxes are disabled, and stop commands.
  let seek_active = [];
  d3.selectAll("#drivers input.set-seek-active")
    .each(function() {
      seek_active.push(this.checked);
    });
  // If every checkbox is in-active, then stop commands.
  if (seek_active.every(active => !active)) reset_command_sliders();
}

function start_auto_limits (_channel, i) {
  // Don't start if we're not receing position data.
  if (state.channels[i].position.length == 0) return;

  d3.select(`#auto-limits-${i}`).property("disabled", true);
  d3.select(`#set-auto-limits-${i}`).property("disabled", false);
  d3.select(`#reset-auto-limits-${i}`).property("disabled", false);

  state.channels[i].auto_limits = true;
  state.channels[i].auto_min_position = last(state.channels[i].position);
  state.channels[i].auto_max_position = last(state.channels[i].position);
}

function save_auto_limits (_channel, i) {
  d3.select(`#set-min-position-${i}`).property("value", state.channels[i].auto_min_position.toFixed(2));
  d3.select(`#set-max-position-${i}`).property("value", state.channels[i].auto_max_position.toFixed(2));

  send_config();

  stop_auto_limits(_channel, i);
}

function stop_auto_limits (_channel, i) {
  d3.select(`#auto-limits-${i}`).property("disabled", false);
  d3.select(`#set-auto-limits-${i}`).property("disabled", true);
  d3.select(`#reset-auto-limits-${i}`).property("disabled", true);

  state.channels[i].auto_limits = false;
  state.channels[i].auto_min_position = null;
  state.channels[i].auto_max_position = null;

  requestAnimationFrame(show_config);
}

function setup_graphs() {
  d3.select("#drivers")
    .selectAll("div")
    .data(state.channels)
    .join(
      enter => {
        let div = enter.append("div")
          .style("margin", "5px");

        div.append("h4").text((_channel, i) => `Channel ${i}`);

        let svg_ps = div.append("svg")
          .classed("position-seek", true)
          .style("display", "block")
          .attr("width", width)
          .attr("height", height);

        svg_ps.append("line")
          .classed("min-position", true)
          .attr("x1", "0%")
          .attr("x2", "100%")
          .attr("stroke", "aqua")
          .attr("stroke-dasharray", "3,3");
        svg_ps.append("line")
          .classed("max-position", true)
          .attr("x1", "0%")
          .attr("x2", "100%")
          .attr("stroke", "aqua")
          .attr("stroke-dasharray", "3,3");


        svg_ps.append("path")
          .classed("position", true)
          .attr("stroke", "royalblue")
          .attr("fill", "none");
        svg_ps.append("path")
          .classed("seek", true)
          .attr("stroke", "deepskyblue")
          .attr("stroke-dasharray", "1,1")
          .attr("fill", "none");


        let svg_pc = div.append("svg")
          .classed("power-current", true)
          .style("display", "block")
          .attr("width", width)
          .attr("height", height);

        svg_pc.append("line")
          .attr("x1", "0%")
          .attr("x2", "100%")
          .attr("y1", power_scale(0.0))
          .attr("y2", power_scale(0.0))
          .attr("stroke", "darkorchid")
          .attr("stroke-dasharray", "3,3");

        svg_pc.append("line")
          .attr("x1", "0%")
          .attr("x2", "100%")
          .attr("y1", current_scale(0.0))
          .attr("y2", current_scale(0.0))
          .attr("stroke", "orangered")
          .attr("stroke-dasharray", "3,3");

        svg_pc.append("path")
          .classed("power", true)
          .attr("stroke", "darkorchid")
          .attr("fill", "none");
        svg_pc.append("path")
          .classed("current", true)
          .attr("stroke", "orangered")
          .attr("fill", "none");


        let inputs_grid = div.append("div")
          .style("display", "grid")
          .style("grid-template-columns", "1fr 2fr")
          .style("grid-column-gap", "10px");

        // Power
        inputs_grid.append("label")
          .text("Power:");

        inputs_grid.append("input")
          .classed("set-power", true)
          .attr("type", "range")
          .attr("min", "0")
          .attr("value", "10")
          .attr("max", "20")
          .attr("step", "1")
          .on("input", start_command_sliders)
          .on("change", function() {
            // Reset this slider to it's center position.
            this.value = 10;
            // Stop commands if all conditions are met.
            reset_command_sliders_if_not_seeking();
          });


        // Seek
        let seek_label = inputs_grid.append("label");

        seek_label.append("input")
          .classed("set-seek-active", true)
          .attr("id", (_channel, i) => `set-seek-active-${i}`)
          .attr("type", "checkbox")
          .on("input", function() {
            if (this.checked) {
              // If this seek checkbox was set, then start streaming seek commands.
              start_command_sliders();
            } else {
              // Possibly stop commands if not seeking.
              reset_command_sliders_if_not_seeking();
            }
          });

        seek_label.append("span")
          .text("Seek:");

        inputs_grid.append("input")
          .classed("set-seek", true)
          .attr("id", (_channel, i) => `set-seek-${i}`)
          .attr("type", "range")
          .attr("min", "0")
          .attr("value", "10")
          .attr("max", "20")
          .attr("step", "1")
          .on("input", function (_channel, i) {
            d3.select(`#set-seek-active-${i}`).property("checked", true);
            start_command_sliders();
          });


        inputs_grid.append("span")
          .text("Limits:");

        let limits_span = inputs_grid.append("span");

        limits_span.append("input")
          .classed("set-min-position", true)
          .attr("id", (_channel, i) => `set-min-position-${i}`)
          .attr("type", "number")
          .attr("min", "0")
          .attr("max", "1")
          .attr("step", "0.01")
          .style("width", "4em")
          .on("change", send_config);

        limits_span.append("span").text(" - ");

        limits_span.append("input")
          .classed("set-max-position", true)
          .attr("id", (_channel, i) => `set-max-position-${i}`)
          .attr("type", "number")
          .attr("min", "0")
          .attr("max", "1")
          .attr("step", "0.01")
          .style("width", "4em")
          .on("change", send_config);


        inputs_grid.append("button")
          .classed("auto-limits", true)
          .attr("id", (_channel, i) => `auto-limits-${i}`)
          .text("Auto")
          .on("click", start_auto_limits);

        let auto_limits_span = inputs_grid.append("span");

        auto_limits_span.append("button")
          .classed("set-auto-limits", true)
          .attr("id", (_channel, i) => `set-auto-limits-${i}`)
          .text("Set")
          .property("disabled", true)
          .on("click", save_auto_limits);

        auto_limits_span.append("span").text(" - ");

        auto_limits_span.append("button")
          .classed("reset-auto-limits", true)
          .attr("id", (_channel, i) => `reset-auto-limits-${i}`)
          .text("Reset")
          .property("disabled", true)
          .on("click", stop_auto_limits);


        inputs_grid.append("span")
          .text("Reverse:");

        let reverse_span = inputs_grid.append("span");

        let reverse_input_label = reverse_span.append("label");

        reverse_input_label.append("input")
          .classed("set-reverse-input", true)
          .attr("id", (_channel, i) => `set-reverse-input-${i}`)
          .attr("type", "checkbox")
          .on("change", send_config);

        reverse_input_label.append("span")
          .text("input");

        reverse_span.append("span").text(" - ");

        let reverse_output_label = reverse_span.append("label");

        reverse_output_label.append("input")
          .classed("set-reverse-output", true)
          .attr("id", (_channel, i) => `set-reverse-output-${i}`)
          .attr("type", "checkbox")
          .on("change", send_config);

        reverse_output_label.append("span")
          .text("output");
      }
    );

    d3.select("#gauges")
    .selectAll("div")
    .data(state.pressures)
    .join(
      enter => {
        let div = enter.append("div")
          .style("margin", "5px");

        div.append("h4").text((_channel, i) => `Gauge ${i}`);

        let svg = div.append("svg")
          .classed("pressure", true)
          .style("display", "block")
          .attr("width", width)
          .attr("height", height);

        svg.append("path")
          .classed("pressure", true)
          .attr("stroke", "royalblue")
          .attr("fill", "none");
      }
    );


    let config_buttons_div = d3.select("#config");
    config_buttons_div.append("button")
      .attr("id", "save-config")
      .text("Save Configuration")
      .on("click", () => {send_config(true)})
      .property("disabled", true);

      config_buttons_div.append("button")
      .attr("id", "reload-config")
      .text("Reload Configuration")
      .on("click", () => {
        state.initial_configuration_received = false;
        d3.select("#save-config").property("disabled", true);
        reload_config();
      });
}

// Setup graphs and show initial state.
setup_graphs();
show_state();
show_config();


// Time (seconds) after the last update when we attempt reconnecting.
const no_update_timeout = 0.500;
// Time (seconds) to allow for the first update to get to us before attempting a reconnect.
const first_update_allowance = 2.0;


// Check whether we're actively receiving updates.
function connection_problem(){
  let duration_since_connecting = (Date.now() - state.last_connect_local_time) * 0.001;

  // Reconnect if last update was too long ago. Force socket to close, which will cause a reconnect.
  if (state.duration_since_last_update > no_update_timeout) {
    // Allow some time for the first update to come.
    if (duration_since_connecting > first_update_allowance) return true;
  }

  // We're receving states or just connected, so no problem.
  return false;
}

function connect(){
  let new_socket = new WebSocket(url);

  d3.select("#status").text("connecting...");

  new_socket.onopen = function (event) {
    // Store time when we openend connection.
    state.last_connect_local_time = Date.now();

    // Update global socket with the newly opened one.
    socket = new_socket;

    d3.select("#status").text("connected");

    scan_networks();
    request_config();
  };

  new_socket.onclose = function (event) {
    // Disable global socket.
    socket = null;

    d3.select("#status").text("disconnected");

    // Immediately attempt to reconnect on the next event loop.
    setTimeout(connect, 0);
  };

  new_socket.onerror = function (err) {
    console.error(`Socket error: ${err}`);
    new_socket.close();
  };

  new_socket.onmessage = async function (event) {
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
        receive_networks(data);
        return;
      case STATE:
        receive_state(data);
        return;
      case CONFIGURATION:
        receive_config(data);
        return;
      default:
        console.warn(`Unknown code: ${code}`);
        return;
    }
  };
}

connect();

// Update state and check if we need to reconnect.
function update(){
  requestAnimationFrame(update_and_show_state);

  // Force disconnect; which will trigger a reconnect.
  if (connection_problem()) close();
}

// Keep asking for state updates. The cutoff on the chip side is 200ms, so
// if we ask every 80ms we should always recive updates. The `send` function
// guards against sending to an uninitialzed/unconnected socket.
setInterval(request_state_updates, 100);

// Run state updates at 200fps, drawing only happens through requestAnimationFrame.
setInterval(update, 5);