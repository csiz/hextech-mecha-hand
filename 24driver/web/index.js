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
const REGISTER_STATE_UPDATES = 0x07; // Register for state updates.

let socket = new WebSocket("ws://" + location.host + "/ws");

function scan_networks() {
  let data = new Uint8Array(1);
  data[0] = SCAN_NETWORKS;
  socket.send(data.buffer);
}

function register_state_updates(){
  let data = new Uint8Array(1);
  data[0] = REGISTER_STATE_UPDATES;
  socket.send(data.buffer);
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
  socket.send(data.buffer);
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
// Skip up to 100ms of the latest data to avoid shakiness.
const min_duration = 0.1;

// Store the state measurements from the chip.
let state = {
  voltage: 0.0,
  current: 0.0,
  power: 0.0,
  fps: 0.0,
  max_loop_time: 0.0,
  last_update_time: 0.0,
  last_update_local_time: Date.now(),
  duration_since_last_update: 0.0,

  // List of all update times.
  times: [],
  // Duration from previous update to last update (ie. 4 seconds ago).
  durations_to_last: [],
  // Duration from update to now.
  durations_to_now: [],

  // A 24 length array of drive channel measurements and power setting.
  channels: d3.range(24).map((i) => ({position: [], current: [], power: [], seek: []})),
  // A 12 length array of pressure measurements.
  pressures: d3.range(12).map((i) => ({strain: []}))
};

function update_state() {
  state.duration_since_last_update = (Date.now() - state.last_update_local_time) * 0.001;

  // Recompute durations since this update; using uint32 arithmetic, and converting ms to s.
  state.durations_to_last = state.times.map(t => uint32_time_interval(t, state.last_update_time) * 0.001);
  state.durations_to_now = state.durations_to_last.map(t => t + state.duration_since_last_update);

  // Assume times are ordered, and therefore durations is monotonically decreasing.
  // Check how many update are no longer relevant and remove them.
  let stale = 0;
  while (// We have to check that we're not overshooting the list.
    stale < state.durations_to_now.length &&
    state.durations_to_now[stale] > (max_duration + min_duration)) stale++;

  // Usually `stale` should be 1 if we get updates consistently; so using shift is not too shabby.
  state.times = state.times.slice(stale);
  state.durations_to_last.slice(stale);
  state.durations_to_now.slice(stale);
  for (let j = 0; j < 24; j++) {
    state.channels[j].position = state.channels[j].position.slice(stale);
    state.channels[j].current = state.channels[j].current.slice(stale);
    state.channels[j].power = state.channels[j].power.slice(stale);
    state.channels[j].seek = state.channels[j].seek.slice(stale);
  }
  for (let j = 0; j < 12; j++) {
    state.pressures[j].strain.slice(stale);
  }
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
  state.max_loop_time = data.getUint32(offset);
  offset += 4;
  state.last_update_time = data.getUint32(offset);
  offset += 4;

  state.times.push(state.last_update_time);

  state.last_update_local_time = Date.now();

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

  update_state();
}


const width = 200;
const height = 50;
// Hide latest few ms of state updates to avoid flickering.
const time_scale = d3.scaleLinear([max_duration, min_duration], [0.0, width]);
const position_seek_scale = d3.scaleLinear([0.0, 1.0], [0.0, height]);
const power_scale = d3.scaleLinear([-1.0, +1.0], [height, 0.0]);
// Current is in Ampere, but usually it's very low. This might overflow.
const current_scale = d3.scaleLinear([0.0, 0.5], [height, 0.0]);

// Use the last update time as long as we're getting updates consistently.
function duration_at_index(i) {
  return state.duration_since_last_update < min_duration
    ? time_scale(state.durations_to_last[i])
    : time_scale(state.durations_to_now[i]);
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



let state_animation_id = null;

function show_state(){
  update_state();

  d3.selectAll("#drivers>div")
    .data(state.channels)
    .each(function (channel, i) {
      let div = d3.select(this);

      div.select("path.position").attr("d", position_line(channel.position));
      div.select("path.seek").attr("d", seek_line(channel.seek));
      div.select("path.power").attr("d", power_line(channel.power));
      div.select("path.current").attr("d", current_line(channel.current));
    });

  state_animation_id = requestAnimationFrame(show_state);
}

let commands_handle = null;

function send_commands() {

  // Get the power level from each slider.
  let set_power = [];
  d3.selectAll("#drivers input.set-power")
    .each(function() {
      set_power.push(this.value * 0.1 - 1.0);
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
    // TODO: implement seeking
    data_view.setFloat32(offset+4, -1.0);
    offset += 8;
  }

  // Send payload.
  socket.send(data.buffer);
}

function start_command_sliders() {
  if (commands_handle != null) clearInterval(commands_handle);
  commands_handle = setInterval(send_commands, 50);
}

function reset_command_sliders(){
  if (commands_handle != null) clearInterval(commands_handle);
  d3.selectAll("#drivers input.set-power").each(function(){
    this.value = 10;
  });
  send_commands();
}

function setup_graphs() {
  d3.select("#drivers")
    .selectAll("div")
    .data(state.channels)
    .join(
      enter => {
        let div = enter.append("div")
          .style("margin", "5px");

        div.append("p").text((_channel, i) => `Channel ${i}`);

        let svg_ps = div.append("svg")
          .classed("position-seek", true)
          .style("display", "block")
          .attr("width", width)
          .attr("height", height);

        svg_ps.append("line")
          .attr("x1", "0%")
          .attr("x2", "100%")
          .attr("y1", position_seek_scale(0.0))
          .attr("y2", position_seek_scale(0.0))
          .attr("stroke", "aqua")
          .attr("stroke-dasharray", "3,3");
        svg_ps.append("line")
          .attr("x1", "0%")
          .attr("x2", "100%")
          .attr("y1", position_seek_scale(1.0))
          .attr("y2", position_seek_scale(1.0))
          .attr("stroke", "aqua")
          .attr("stroke-dasharray", "3,3");


        svg_ps.append("path")
          .classed("position", true)
          .attr("stroke", "royalblue")
          .attr("fill", "none");
        svg_ps.append("path")
          .classed("seek", true)
          .attr("stroke", "deepskyblue")
          .attr("stroke-dasharray", "5,5")
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


        let slide = div.append("input")
          .classed("set-power", true)
          .attr("type", "range")
          .attr("min", "0")
          .attr("value", "10")
          .attr("max", "20")
          .attr("step", "1")
          .on("mousedown", start_command_sliders)
          .on("mouseup", reset_command_sliders);

      }
    );
}

setup_graphs();
show_state();

socket.onopen = function (event) {
  d3.select("#status").text("connected");

  scan_networks();
  d3.select("#scan-networks").on("click", () => scan_networks());
  d3.select("#connect-network").on("click", () => connect_to_network());
  // Keep asking for state updates. The cutoff on the chip side is 50ms, so
  // if we ask every 80ms we should always recive updates.
  setInterval(register_state_updates, 80);

  state_animation_id = requestAnimationFrame(show_state);
};

socket.onclose = function (event) {
  d3.select("#status").text("disconnected");

  cancelAnimationFrame(state_animation_id);
}

socket.onmessage = async function (event) {
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
    default:
      console.warn(`Unknown code: ${code}`);
      return;
  }
};