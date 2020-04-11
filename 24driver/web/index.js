"use strict";

import {select, selectAll, local} from "d3-selection";
import {range} from "d3-array";
import {line} from "d3-shape";
import {scaleLinear} from "d3-scale";

let d3 = {select, selectAll, range, line, scaleLinear};

import {MotorDriver, exp_average, clamp} from "24driver";


// Motor driver setup
// ------------------

// Websocket url.
// const url = `ws://${location.host}/ws`;
const url = `ws://192.168.1.19/ws`;

let driver = new MotorDriver(url);

const motor_channels_indexes = d3.range(24);
const pressure_channels_indexes = d3.range(12);


driver.onconnecting = () => {
  d3.select("#status").text("connecting...");
};

driver.onconnected = () => {
  d3.select("#status").text("connected");
}

driver.onclose = ()=> {
  d3.select("#status").text("disconnected");
}

// Connect to remote motor driver.
driver.connect();

// Check for errors and reconnect.
setInterval(() => { driver.check_connection(); }, 100);


// Driver wifi management
// ----------------------

d3.select("#scan-networks").on("click", () => {
  driver.scan_networks();
});

driver.onnetworks = (networks) => {
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
};


d3.select("#connect-network").on("click", () => {
  let router_selection = d3.select("#connection-type").select('input:checked').node().value;
  let connect_to_router = (router_selection == "router") ? true : false;
  let ssid = d3.select("#ssid").node().value;
  let pass = d3.select("#pass").node().value;

  driver.connect_to_network(ssid, pass, connect_to_router);
});


// Config
// ------

function show_config() {
  // Skip if config not received.
  if (driver.config == null) return;

  let config = driver.config;

  d3.selectAll("#drivers>div")
  .data(config.motor_channels)
  .each(function (channel) {
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

driver.onconfig = (config) => {
  d3.select("#save-config").property("disabled", false);

  show_config();
};


function send_config(save = false){
  // Don't send anything until the first config is received.
  if (driver.config == null) return;

  // Get the limits from the input fields.
  d3.selectAll("#drivers input.set-min-position")
    .each(function(_channel, i) {
      driver.config.motor_channels[i].min_position = parseFloat(this.value);
    });
  d3.selectAll("#drivers input.set-max-position")
    .each(function(_channel, i) {
      driver.config.motor_channels[i].max_position = parseFloat(this.value);
    });

  // Get channel reversals (whether the cables are connected the other way around).
  d3.selectAll("#drivers input.set-reverse-input")
    .each(function(_channel, i) {
      driver.config.motor_channels[i].reverse_input = Boolean(this.checked);
    });
  d3.selectAll("#drivers input.set-reverse-output")
    .each(function(_channel, i) {
      driver.config.motor_channels[i].reverse_output = Boolean(this.checked);
    });

  // TODO: configure strain gauges too.

  driver.send_config(driver.config, save);

  // Update the UI with what we sent.
  show_config();
}

// Auto limits scan for the minimum position and maximum position and sets the config.
let motor_channel_limits = motor_channels_indexes.map((i) => ({
  auto_limits: false,
  auto_min_position: null,
  auto_max_position: null,
}));


function start_auto_limits(i) {
  // Don't start if we're not receing data.
  if (driver.state == null) return;

  d3.select(`#auto-limits-${i}`).property("disabled", true);
  d3.select(`#set-auto-limits-${i}`).property("disabled", false);
  d3.select(`#reset-auto-limits-${i}`).property("disabled", false);

  motor_channel_limits[i].auto_limits = true;
  motor_channel_limits[i].auto_min_position = driver.state.motor_channels[i].position;
  motor_channel_limits[i].auto_max_position = driver.state.motor_channels[i].position;
}

function save_auto_limits(i) {
  d3.select(`#set-min-position-${i}`).property("value", motor_channel_limits[i].auto_min_position.toFixed(2));
  d3.select(`#set-max-position-${i}`).property("value", motor_channel_limits[i].auto_max_position.toFixed(2));

  send_config();

  stop_auto_limits(i);
}

function stop_auto_limits(i) {
  d3.select(`#auto-limits-${i}`).property("disabled", false);
  d3.select(`#set-auto-limits-${i}`).property("disabled", true);
  d3.select(`#reset-auto-limits-${i}`).property("disabled", true);

  motor_channel_limits[i].auto_limits = false;
  motor_channel_limits[i].auto_min_position = null;
  motor_channel_limits[i].auto_max_position = null;

  show_config();
}


// Drive commands
// --------------

driver.onsendcommands = () => {

  // Get the power level from each slider.
  let power = [];
  d3.selectAll("#drivers input.set-power")
    .each(function() {
      power.push(this.value * 0.1 - 1.0);
    });

  // Get whether channels is actively seeking.
  let seek_active = [];
  d3.selectAll("#drivers input.set-seek-active")
    .each(function() {
      seek_active.push(this.checked);
    });

  // Get seek positions.
  let seek = [];
  d3.selectAll("#drivers input.set-seek")
    .each(function(_channel, i) {
      // Get [0.0, 1.0] seek position from the slider.
      let seek_position = this.value * 0.05;
      seek.push(seek_active[i] ? seek_position : null);
    });

  // Update driver commands to be sent.
  driver.commands = {power, seek};
}

function start_command_sliders() {
  driver.command();
}

function reset_command_sliders(){
  driver.release();

  // Reset power levels to 0.
  d3.selectAll("#drivers input.set-power").each(function(){
    this.value = 10;
  });
  // Disable seeking, but note that we leave all seek positions as they are.
  d3.selectAll("#drivers input.set-seek-active").property("checked", false);

  // Send the inputs that were reset.
  driver.send_commands();
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


// State updates
// -------------

// ### Graph dimensions

const width = 300;
const height = 80;

// Show up to 5 seconds of data.
const max_duration = 5000;
// Ignore first few milliseconds to avoid flickering from slightly delayed data.
const min_duration = 100;


// Hide latest few ms of state updates to avoid flickering.
const time_scale = d3.scaleLinear([max_duration, min_duration], [0.0, width]);
const position_seek_scale = d3.scaleLinear([0.0, 1.0], [height-1, 1]);
const power_scale = d3.scaleLinear([-1.0, +1.0], [height-1, 1]);
// Current is in Ampere, but usually it's very low. This might overflow.
const current_scale = d3.scaleLinear([0.0, 0.5], [height-1, 1]);

// Strain scale in Volts (TODO change to some force unit maybe Newton, after calibrating).
const pressure_scale = d3.scaleLinear([-0.01, 0.01], [height-1, 1]);




// Compute difference between locally elapsed time and the driver elapsed time. If
// there are bursts of lag, we can use this to smooth out the graph times.
let smooth_difference_in_elapsed = 0.0;

driver.onstate = (state) => {

  // Adjust for network latency, see `time_position_at_index` for explanation.

  // Compute smoothed elapsed time differences, but only if they're small enough.
  let diff = state.local_elapsed - state.driver_elapsed;

  // Smoothed time difference between chip time and local time. We need this
  // to avoid graphs jumping around due to different latency between state updates.
  smooth_difference_in_elapsed = clamp(
    exp_average(diff, smooth_difference_in_elapsed, 0.9),
    // Clamp the difference to at most a half second interval either side.
    -500, 500);

  // Update auto limits if enabled.
  for (let i = 0; i < 24; i++) {
    let channel = state.motor_channels[i];
    let channel_limits = motor_channel_limits[i];
    if (channel_limits.auto_limits) {
      channel_limits.auto_min_position = Math.min(channel_limits.auto_min_position, last(channel.position));
      channel_limits.auto_max_position = Math.max(channel_limits.auto_max_position, last(channel.position));
    }
  }
}


function show_state(){

  // Wait until the first state is received.
  if (driver.state == null) return;

  let states = driver.state_history;


  // Get current time; we'll advance the graphs even when no new updates are coming.
  let now = Date.now();


  // Get the pixel position for the elapsed time at index i.
  function time_position_at_index(i) {
    // Adjust time to account for spiky network latency.
    //
    // If a loop elapsed time on the esp32 is lower than update elapsed time in the browser
    // then there was extra latency on the network. We should compensate for this by adding the
    // difference. Since the difference is a crude estimate we need to smooth it out over time.
    // Note that it should always oscillate around 0s difference, which would be the case if
    // there was perfectly constant latency between esp32 updates and the browser.
    return time_scale(now - states[i].local_time + smooth_difference_in_elapsed);
  }


  let position_line = d3.line()
    .x((_, i) => time_position_at_index(i))
    .y(p => position_seek_scale(p));

  let seek_line = d3.line()
    .x((_, i) => time_position_at_index(i))
    .y(s => position_seek_scale(s))
    .defined(s => s != -1.0);

  let power_line = d3.line()
    .x((_, i) => time_position_at_index(i))
    .y(p => power_scale(p));

  let current_line = d3.line()
    .x((_, i) => time_position_at_index(i))
    .y(c => current_scale(c));

  let pressure_line = d3.line()
    .x((_, i) => time_position_at_index(i))
    .y(c => pressure_scale(c));



  // TODO: show power and timing info
  d3.selectAll("#drivers>div")
    .data(motor_channels_indexes)
    .each(function (i) {
      let div = d3.select(this);

      div.select("path.position").attr("d", position_line(states.map(s => s.motor_channels[i].position)));
      div.select("path.seek").attr("d", seek_line(states.map(s => s.motor_channels[i].seek)));
      div.select("path.power").attr("d", power_line(states.map(s => s.motor_channels[i].power)));
      div.select("path.current").attr("d", current_line(states.map(s => s.motor_channels[i].current)));

      // If we're tracking position limits, then take over the limit lines.
      if (motor_channel_limits[i].auto_limits) {
        div.select("line.min-position")
          .attr("y1", position_seek_scale(motor_channel_limits[i].auto_min_position))
          .attr("y2", position_seek_scale(motor_channel_limits[i].auto_min_position));
        div.select("line.max-position")
          .attr("y1", position_seek_scale(motor_channel_limits[i].auto_max_position))
          .attr("y2", position_seek_scale(motor_channel_limits[i].auto_max_position));
      }

    });

  d3.selectAll("#gauges>div")
    .data(pressure_channels_indexes)
    .each(function (i) {
      let div = d3.select(this);

      div.select("path.pressure").attr("d", pressure_line(states.map(s => s.pressure_channels[i].strain)));
    });
}


// UI setup
// --------

function setup_graphs() {
  d3.select("#drivers")
    .selectAll("div")
    .data(motor_channels_indexes)
    .join(
      enter => {
        let div = enter.append("div")
          .style("margin", "5px");

        div.append("h4").text(i => `Channel ${i}`);

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
          .attr("id", i => `set-seek-active-${i}`)
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
          .attr("id", i => `set-seek-${i}`)
          .attr("type", "range")
          .attr("min", "0")
          .attr("value", "10")
          .attr("max", "20")
          .attr("step", "1")
          .on("input", function (i) {
            d3.select(`#set-seek-active-${i}`).property("checked", true);
            start_command_sliders();
          });


        inputs_grid.append("span")
          .text("Limits:");

        let limits_span = inputs_grid.append("span");

        limits_span.append("input")
          .classed("set-min-position", true)
          .attr("id", i => `set-min-position-${i}`)
          .attr("type", "number")
          .attr("min", "0")
          .attr("max", "1")
          .attr("step", "0.01")
          .style("width", "4em")
          .on("change", send_config);

        limits_span.append("span").text(" - ");

        limits_span.append("input")
          .classed("set-max-position", true)
          .attr("id", i => `set-max-position-${i}`)
          .attr("type", "number")
          .attr("min", "0")
          .attr("max", "1")
          .attr("step", "0.01")
          .style("width", "4em")
          .on("change", send_config);


        inputs_grid.append("button")
          .classed("auto-limits", true)
          .attr("id", i => `auto-limits-${i}`)
          .text("Auto")
          .on("click", start_auto_limits);

        let auto_limits_span = inputs_grid.append("span");

        auto_limits_span.append("button")
          .classed("set-auto-limits", true)
          .attr("id", i => `set-auto-limits-${i}`)
          .text("Set")
          .property("disabled", true)
          .on("click", save_auto_limits);

        auto_limits_span.append("span").text(" - ");

        auto_limits_span.append("button")
          .classed("reset-auto-limits", true)
          .attr("id", i => `reset-auto-limits-${i}`)
          .text("Reset")
          .property("disabled", true)
          .on("click", stop_auto_limits);


        inputs_grid.append("span")
          .text("Reverse:");

        let reverse_span = inputs_grid.append("span");

        let reverse_input_label = reverse_span.append("label");

        reverse_input_label.append("input")
          .classed("set-reverse-input", true)
          .attr("id", i => `set-reverse-input-${i}`)
          .attr("type", "checkbox")
          .on("change", send_config);

        reverse_input_label.append("span")
          .text("input");

        reverse_span.append("span").text(" - ");

        let reverse_output_label = reverse_span.append("label");

        reverse_output_label.append("input")
          .classed("set-reverse-output", true)
          .attr("id", i => `set-reverse-output-${i}`)
          .attr("type", "checkbox")
          .on("change", send_config);

        reverse_output_label.append("span")
          .text("output");
      }
    );

    d3.select("#gauges")
    .selectAll("div")
    .data(pressure_channels_indexes)
    .join(
      enter => {
        let div = enter.append("div")
          .style("margin", "5px");

        div.append("h4").text(i => `Gauge ${i}`);

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
        driver.config = null;
        d3.select("#save-config").property("disabled", true);
        reload_config();
      });
}

// Setup graphs and start the state loop.
setup_graphs();


function update() {
  requestAnimationFrame(update);
  show_state();
}

requestAnimationFrame(update);
