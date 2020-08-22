"use strict";

import * as d3 from "d3";
import * as THREE from "three";

import {MotorDriver, deinterpolate, zero_commands} from "24driver";
import { local } from "d3";
import { Vector3 } from "three";

// Hand data
// ---------

function make_default_digit_state (){
  return {
    valid: false,

    leap_finger_id: null,
    leap_finger_type: null,

    distal_tip: null,
    distal_basis: null,

    intermediate_tip: null,
    intermediate_basis: null,

    proximal_tip: null,
    proximal_basis: null,

    metacarpal_tip: null,
    metacarpal_basis: null,
  };
};

function make_default_hand_state () {
  return {
    valid: false,

    leap_hand_id: null,

    // Matrix4 that determines the local coordinates of the palm and digits.
    //
    // The local frame is determined by the wrist direction except is's axial rotation and the
    // palm's coordinates. The local transform discards the above component to get the local
    // position and orientation of each component.
    local_transform: null,
    world_transform: null,

    arm_world_basis: null,

    palm_world_basis: null,

    palm_basis: null,

    palm_world_position: null,

    palm_position: null,

    palm_pitch: null,
    palm_roll: null,
    palm_yaw: null,

    thumb_pivot: null,
    pinky_pivot: null,


    digits: {
      thumb: make_default_digit_state(),
      index: make_default_digit_state(),
      middle: make_default_digit_state(),
      ring: make_default_digit_state(),
      pinky: make_default_digit_state(),
    },
  };
};


let hand_state = make_default_hand_state();

/* Signed angle between two vectors projected on the plane.

Formula copied from: https://stackoverflow.com/a/33920320/103054 */
function signed_angle_on_plane(Va, Vb, plane_normal) {

  let Pa = Va.clone().projectOnPlane(plane_normal);
  let Pb = Vb.clone().projectOnPlane(plane_normal);

  return Math.atan2(new THREE.Vector3().crossVectors(Pb, Pa).dot(plane_normal), Pa.dot(Pb));
}

function plane_normal(Va, Vb) {
  return new THREE.Vector3().crossVectors(Va, Vb).normalize();
}

/* Convenince function to extract the basis into 3 vectors. */
function get_basis_vectors(m4) {
  let x_axis = new THREE.Vector3();
  let y_axis = new THREE.Vector3();
  let z_axis = new THREE.Vector3();
  m4.extractBasis(x_axis, y_axis, z_axis);
  return {x_axis, y_axis, z_axis}
}

let control_arm = false;
let forearm_length = 200;
let shoulder_height = 300;
let shoulder_offset = -300;
let shoulder_distance = 400;

function parse_hand_state(leap_state){
  // No hands in view, return invalid state.
  if (leap_state.hands === undefined || leap_state.hands.length < 1) return make_default_hand_state();

  const leap_hand = leap_state.hands[0];

  // Only accept right hand for now. Sometimes it detects it as a left hand, but with messed up finger
  // positions. We can avoid some spazzing by requiring right hand strictly.
  if (leap_hand.type != "right") return make_default_hand_state();

  // Remember the hand id, we need it to match the fingers.
  const leap_hand_id = leap_hand.id;

  // We'll use the basis of the arm as origin axis.
  const arm_world_basis = new THREE.Matrix4().makeBasis(
    ...leap_hand.armBasis.map(vec => new THREE.Vector3(...vec)));


  // Get the position of the wrist, assuming it's aligned with the arm basis.
  const wrist_world_position = new THREE.Vector3(... leap_hand.wrist);


  // Get palm coordinates and basis.
  const palm_world_position = new THREE.Vector3(...leap_hand.palmPosition);

  // Leap's direction points to the fingers, but the coordinate system points outside of screen.
  const forward_direction = new THREE.Vector3(...leap_hand.direction);
  const direction = forward_direction.clone().negate();
  // The palm normal points downards from the palm, but the coordinate system points up.
  const palm_grasp_normal = new THREE.Vector3(... leap_hand.palmNormal);
  const normal = palm_grasp_normal.clone().negate();
  // Get the 3rd basis using the right hand rule.
  const lateral = new THREE.Vector3().crossVectors(normal, direction).normalize();

  // Create the absolute basis.
  const palm_world_basis = new THREE.Matrix4().makeBasis(lateral, normal, direction);

  // The palm's roll is actually derived from the twisting of the arm in the human body.
  // But the arm isn't fully visible in the leap controller. We'll extract it from the
  // rotation between the vertical axis and the palm normal, with respect to the palm's
  // direction axis.
  const roll = signed_angle_on_plane(normal, new THREE.Vector3(0, 1, 0), direction);

  // Compute the tranform that switches coordinates to the arm perspective. However,
  // we keep the roll, since it's value is derived from the direction of gravity.

  // Because the matrix is orthonormal, we can transpose it to get the inverse.
  const arm_local_transform = arm_world_basis.clone().transpose();

  let local_transform = arm_local_transform.clone()
    .premultiply(new THREE.Matrix4().makeRotationZ(roll));

  // When applying matrix4 the position is set after the rotation. To center the object in
  // a single command, we then need to compute the rotated origin coordinates and negate them.
  local_transform.setPosition(
    wrist_world_position.clone().applyMatrix4(local_transform).negate());

  // Keep a reference to reverse to world coordinates.
  let world_transform = new THREE.Matrix4().getInverse(local_transform);

  // Arm
  // ---

  const elbow_world_position = wrist_world_position.clone()
    .add(get_basis_vectors(arm_world_basis).z_axis.clone().multiplyScalar(forearm_length));
  const shoulder_world_position = new THREE.Vector3(shoulder_offset, shoulder_height, shoulder_distance);

  let elbow_to_shoulder = new THREE.Vector3().subVectors(shoulder_world_position, elbow_world_position);
  let elbow_to_wrist = new THREE.Vector3().subVectors(wrist_world_position, elbow_world_position);

  // I'm not sure this is signed properly when over 180 degrees, but the expected value for usual case is around 90 degree bend, which works fine.
  const elbow_angle = signed_angle_on_plane(elbow_to_wrist, elbow_to_shoulder, plane_normal(elbow_to_shoulder, elbow_to_wrist));

  // Get the twisting of the arm by measuring the angle between vertical and the forearm, in the plane where the arm is perpendicular.
  const arm_twist = signed_angle_on_plane(elbow_to_wrist, new Vector3(0, 1, 0), elbow_to_shoulder.clone().normalize());

  // Get the tilt of the arm away from the body in plane of the body (forward direction in our coordinates).
  const arm_tilt = signed_angle_on_plane(elbow_to_shoulder, new Vector3(0, 1, 0), new Vector3(0, 0, 1));

  // Get the arm pivot angle. How far the arm is streched in front in the plane cutting the body in half.
  const arm_pivot = signed_angle_on_plane(elbow_to_shoulder, new Vector3(0, 1, 0), new Vector3(1, 0, 0));


  // Wrist and fingers
  // -----------------

  const wrist_position = wrist_world_position.clone().applyMatrix4(local_transform);
  const arm_basis = arm_world_basis.clone().premultiply(local_transform);

  // Get the palm basis, in the reference frame of the arm.
  const palm_position = palm_world_position.clone().applyMatrix4(local_transform);
  const palm_basis = palm_world_basis.clone().premultiply(local_transform);

  // Get the remaining Euler angles for the wrist/palm. The physical model of the hand
  // applies roll (Z) followed by yaw (Y) and finally pitch (X); use the same axis order.
  const palm_angles = new THREE.Euler().setFromRotationMatrix(palm_basis, "ZYX");
  const palm_roll = palm_angles.z;
  const palm_yaw = -palm_angles.y;
  const palm_pitch = palm_angles.x;
  // We should already have palm roll with respect to gravity. There is a small discrepancy
  // in the leap controller between the roll for the arm and the roll for the hand. But it
  // seems like we're really close; agreeing up to 2 decimal places in degrees.


  // Get the pointables for this hand.
  const leap_fingers = leap_state.pointables.filter(p => p.handId == leap_hand_id);

  // Parse all fingers equally at first. We'll match them by type later (thumb vs pinkie).
  const fingers = leap_fingers.map(leap_finger => {

    const leap_finger_id = leap_finger.id;
    const leap_finger_type = leap_finger.type;

    // There are 4 bases for each finger corresponding to the metacarpal, proximal phalanx,
    // medial phalanx and distal phalanx. The thumb is represented with a 0 length metacarpal.
    let bases = leap_finger.bases.map(
      base => new THREE.Matrix4().makeBasis(
        ...base.map(vec => new THREE.Vector3(...vec))
      )
    );
    if (bases.length != 4) return make_default_digit_state();

    const distal_tip = new THREE.Vector3(...leap_finger.btipPosition).applyMatrix4(local_transform);
    const distal_basis = bases[3].premultiply(local_transform);
    const intermediate_tip = new THREE.Vector3(...leap_finger.dipPosition).applyMatrix4(local_transform);
    const intermediate_basis = bases[2].premultiply(local_transform);
    const proximal_tip = new THREE.Vector3(...leap_finger.pipPosition).applyMatrix4(local_transform);
    const proximal_basis = bases[1].premultiply(local_transform);
    const metacarpal_tip = new THREE.Vector3(... leap_finger.mcpPosition).applyMatrix4(local_transform);
    const metacarpal_basis = bases[0].premultiply(local_transform);

    // Get base angles from the angle between the proximal and metacarpal.
    const proximal_angles = new THREE.Euler().setFromRotationMatrix(
      proximal_basis.clone().premultiply(metacarpal_basis.clone().transpose()),
      "YXZ");

    const yaw = -proximal_angles.y;
    const pitch = -proximal_angles.x;

    // Get the curl of the finger from the angle betwen the tip and proximal.
    const tip_angles = new THREE.Euler().setFromRotationMatrix(
      distal_basis.clone().premultiply(proximal_basis.clone().transpose()),
      "XYZ");

    const curl = -tip_angles.x;


    return {
      valid: true,
      leap_finger_id,
      leap_finger_type,
      distal_tip,
      distal_basis,
      intermediate_tip,
      intermediate_basis,
      proximal_tip,
      proximal_basis,
      metacarpal_tip,
      metacarpal_basis,

      yaw,
      pitch,
      curl,
    }
  });

  function pick_finger_type(type) {
    let typed_fingers = fingers.filter(f => f.leap_finger_type == type);
    return typed_fingers.length == 1 ? typed_fingers[0] : make_default_digit_state();
  };

  let digits = {
    thumb: pick_finger_type(0),
    index: pick_finger_type(1),
    middle: pick_finger_type(2),
    ring: pick_finger_type(3),
    pinky: pick_finger_type(4),
  };


  let thumb_pivot = digits.thumb.valid
    ? signed_angle_on_plane(new THREE.Vector3().subVectors(digits.thumb.proximal_tip, palm_position), lateral.clone().negate(), direction)
    : null;

  let pinky_pivot = digits.pinky.valid
    ? signed_angle_on_plane(new THREE.Vector3().subVectors(digits.pinky.proximal_tip, palm_position), lateral, direction)
    : null;


  return {
    valid: true,
    leap_hand_id,
    local_transform,
    world_transform,

    elbow_world_position,
    shoulder_world_position,
    elbow_angle,
    arm_twist,
    arm_tilt,
    arm_pivot,

    arm_world_basis,
    arm_basis,
    wrist_position,

    palm_world_basis,
    palm_basis,
    palm_world_position,
    palm_position,
    palm_pitch,
    palm_roll,
    palm_yaw,
    thumb_pivot,
    pinky_pivot,

    digits,
  };
};



// Connection state.
let leap_socket = null;
let focus_listener = null;
let blur_listener = null;

function leap_connect(){
  leap_socket = new WebSocket("ws://localhost:6437/v7.json");

  // Get leap service focus when connecting.
  leap_socket.onopen = function(event) {
    leap_socket.send(JSON.stringify({focused: true})); // claim focus

    window.addEventListener('focus', function(e) {
      leap_socket.send(JSON.stringify({focused: true})); // claim focus
    });

    window.addEventListener('blur', function(e) {
      leap_socket.send(JSON.stringify({focused: false})); // relinquish focus
    });
  };

  // Parse leap hand data.
  leap_socket.onmessage = function(event) {
    let leap_state = JSON.parse(event.data);
    if(leap_state.timestamp === undefined){
      console.warn(`Unknown message from leap service: ${event.data}`);
      return;
    }

    hand_state = parse_hand_state(leap_state);
    update_commands();
  };

  // On socket close
  leap_socket.onclose = function(event) {
    leap_socket = null;
    window.removeEventListener("focus", focus_listener);
    window.removeEventListener("blur", blur_listener);

    hand_state = make_default_hand_state();

    // Attempt a reconnect in a second.
    setTimeout(leap_connect, 1000);
  }

  // On socket error
  leap_socket.onerror = function(event) {
    alert("Leap socket error.");
  };

};

leap_connect();



// Hand Graphics
// -------------

const width = 1280;
const height = 720;
const ratio = width / height;

// const camera_size = 500;
// let camera = new THREE.OrthographicCamera(-0.5*ratio*camera_size, +0.5*ratio*camera_size, +0.5*camera_size, -0.5*camera_size, 0.0, 5000);
let camera = new THREE.PerspectiveCamera(60, ratio, 1, 5000);
camera.position.z = 600;
camera.position.y = 800;
camera.lookAt(0, 100, 0);


let renderer = new THREE.WebGLRenderer({antialias: true, alpha: true});
renderer.setSize(width, height);

d3.select("main").append(() => renderer.domElement);



let scene = new THREE.Scene();

let main_color = "royalblue";
let edge_color = "black";

function make_edgy_cube(w, h, d, color, edge_color) {
  let cube_geometry = new THREE.BoxGeometry(w, h, d);
  let cube_material = new THREE.MeshBasicMaterial({color});
  let cube = new THREE.Mesh(cube_geometry, cube_material);

  let cube_edges_geometry = new THREE.EdgesGeometry(cube_geometry);
  let cube_edges_material = new THREE.LineBasicMaterial({color: edge_color});
  let cube_edges = new THREE.LineSegments(cube_edges_geometry, cube_edges_material);
  cube_edges.renderOrder = 1;

  cube.add(cube_edges);
  return cube;
};

function make_edgy_sphere(r, color, edge_color) {
  let sphere_geometry = new THREE.SphereGeometry(r, 16, 16);
  let sphere_material = new THREE.MeshBasicMaterial({color});
  let sphere = new THREE.Mesh(sphere_geometry, sphere_material);

  let sphere_edges_geometry = new THREE.EdgesGeometry(sphere_geometry);
  let sphere_edges_material = new THREE.LineBasicMaterial({color: edge_color});
  let sphere_edges = new THREE.LineSegments(sphere_edges_geometry, sphere_edges_material);
  sphere_edges.renderOrder = 1;

  sphere.add(sphere_edges);

  return sphere;
}

var hand_model = new THREE.Object3D();
hand_model.matrixAutoUpdate = false;
scene.add(hand_model);

let palm_cube = make_edgy_cube(40, 5, 40, "orangered", edge_color);
hand_model.add(palm_cube);


let wrist_cube = make_edgy_cube(20, 20, 10, "orangered", edge_color);
hand_model.add(wrist_cube);


let digit_models = ["thumb", "index", "middle", "ring", "pinky"].map(digit_name => {

  let knuckle_cube = make_edgy_cube(20, 20, 20, main_color, edge_color);
  hand_model.add(knuckle_cube);

  let curl_cube = make_edgy_cube(20, 20, 20, main_color, edge_color);
  hand_model.add(curl_cube);

  let distal_cube = make_edgy_cube(10, 10, 10, main_color, edge_color);
  hand_model.add(distal_cube);

  let tip_cube = make_edgy_cube(20, 20, 20, main_color, edge_color);
  hand_model.add(tip_cube);

  return {
    name: digit_name,
    knuckle_cube,
    curl_cube,
    distal_cube,
    tip_cube,
  };
});

var arm_model = new THREE.Object3D();
arm_model.matrixAutoUpdate = false;
scene.add(arm_model);

let elbow_sphere = make_edgy_sphere(20, "orangered", edge_color);
arm_model.add(elbow_sphere);
let shoulder_sphere = make_edgy_sphere(20, "grey", edge_color)
arm_model.add(shoulder_sphere);



// Animation loop
// --------------

let animate = () => {
  requestAnimationFrame(animate);

  if (hand_state.valid) {
    // Set palm positions.
    hand_model.visible = true;

    palm_cube.position.copy(hand_state.palm_position);
    palm_cube.quaternion.setFromRotationMatrix(hand_state.palm_basis);

    wrist_cube.position.copy(hand_state.wrist_position);
    wrist_cube.quaternion.setFromRotationMatrix(hand_state.arm_basis);

    // Set finger positions.
    for (let digit_model of digit_models) {
      let digit_state = hand_state.digits[digit_model.name];
      if (digit_state.valid) {
        digit_model.knuckle_cube.position.copy(digit_state.metacarpal_tip);
        digit_model.knuckle_cube.quaternion.setFromRotationMatrix(digit_state.metacarpal_basis);

        digit_model.curl_cube.position.copy(digit_state.proximal_tip);
        digit_model.curl_cube.quaternion.setFromRotationMatrix(digit_state.proximal_basis);

        digit_model.distal_cube.position.copy(digit_state.intermediate_tip);
        digit_model.distal_cube.quaternion.setFromRotationMatrix(digit_state.intermediate_basis);

        digit_model.tip_cube.position.copy(digit_state.distal_tip);
        digit_model.tip_cube.quaternion.setFromRotationMatrix(digit_state.distal_basis);
      }
    }

    // If controlling the arm, set the arm positions.
    if (control_arm) {
      // If we control the whole arm, switch to world positioning.
      hand_model.matrix.copy(hand_state.world_transform);
      arm_model.visible = true;


      elbow_sphere.position.copy(hand_state.elbow_world_position);
      shoulder_sphere.position.copy(hand_state.shoulder_world_position);


    } else {
      // If only controlling the hand, fix the wrist to the center of the scene.
      hand_model.matrix.identity();
      arm_model.visible = false;
    }

  } else {
    hand_model.visible = false;
    arm_model.visible = false;
  }



  renderer.render(scene, camera);
};

animate();



// Motor Driver
// ------------

let channels = {
  wrist_roll: 16,
  wrist_yaw: 12,
  wrist_pitch: 8,
  thumb_pivot: 4,
  pinky_pivot: 0,

  pinky_curl: 3,
  pinky_pitch: 2,
  pinky_yaw: 1,

  ring_curl: 7,
  ring_pitch: 6,
  ring_yaw: 5,

  middle_curl: 11,
  middle_pitch: 10,
  middle_yaw: 9,

  index_curl: 15,
  index_pitch: 14,
  index_yaw: 13,

  thumb_curl: 19,
  thumb_pitch: 18,
  thumb_yaw: 17,

  elbow_bend: 20,
  arm_twist: 21,
  arm_front: 22,
  arm_back: 23,
};


let driver = new MotorDriver();

function update_commands(){
  // Reset commands; we'll fill up the valid ones after.
  driver.commands = zero_commands();

  if (!hand_state.valid) return;

  const PI = Math.PI;
  let seek = driver.commands.seek;


  seek[channels.wrist_roll] = -hand_state.palm_roll / (0.8 * PI) + 0.5;
  seek[channels.wrist_yaw] = hand_state.palm_yaw / (0.3 * PI) + 0.3;
  seek[channels.wrist_pitch] = hand_state.palm_pitch / (0.4 * PI) + 0.5;

  if (hand_state.thumb_pivot != null) {
    seek[channels.thumb_pivot] = -hand_state.thumb_pivot / (0.25 * PI) + 1.2;
  }

  if (hand_state.pinky_pivot != null) {
    seek[channels.pinky_pivot] = hand_state.pinky_pivot / (0.6 * PI) + 1.0;
  }

  let {thumb, index, middle, ring, pinky} = hand_state.digits;

  if (pinky.valid) {

    seek[channels.pinky_yaw] = pinky.yaw / (0.2 * PI) + 0.1;
    seek[channels.pinky_pitch] = pinky.pitch / (0.6 * PI) + 0.3;
    seek[channels.pinky_curl] = pinky.curl / (0.6 * PI) - 0.1;

  }

  if (ring.valid) {

    seek[channels.ring_yaw] = ring.yaw / (0.1 * PI) + 0.1;
    seek[channels.ring_pitch] = ring.pitch / (0.6 * PI) + 0.4;
    seek[channels.ring_curl] = ring.curl / (0.6 * PI) + 0.0;

  }

  if (middle.valid) {

    seek[channels.middle_yaw] = middle.yaw / (0.1 * PI) + 0.1;
    seek[channels.middle_pitch] = middle.pitch / (0.6 * PI) + 0.3;
    seek[channels.middle_curl] = middle.curl / (0.6 * PI) + 0.0;

  }

  if (index.valid) {

    seek[channels.index_yaw] = index.yaw / (0.15 * PI) + 0.3;
    seek[channels.index_pitch] = index.pitch / (0.5 * PI) + 0.5;
    seek[channels.index_curl] = index.curl / (0.6 * PI) - 0.0;

  }

  if (thumb.valid) {

    seek[channels.thumb_yaw] = thumb.yaw / (0.20 * PI) + 0.9;
    seek[channels.thumb_pitch] = thumb.pitch / (0.2 * PI) + 0.5;
    seek[channels.thumb_curl] = thumb.curl / (0.4 * PI) + 0.3;

  }



  if (control_arm) {
    // TODO: ...
  }

}

// UI
// --

// ### Present connection status.
driver.onconnecting = () => d3.select("#status").text("Connecting...");
driver.onconnected = () => d3.select("#status").text("Connected.");
driver.onclose = () => d3.select("#status").text("Connection closed!");


let last_ip = localStorage.getItem("mechahand-url");
if (last_ip != null) {
  d3.select("#driver_ip").property("value", last_ip);
  driver.connect(`ws://${last_ip}/ws`);
}

d3.select("#driver_connect").on("click", () => {
  let ip = d3.select("#driver_ip").property("value");
  localStorage.setItem("mechahand-url", ip);
  driver.connect(`ws://${ip}/ws`);
});


// ### Arm control settings

function set_arm_control(enabled){
  control_arm = enabled;
  d3.select("#forearm-length").property("disabled", !enabled);
  d3.select("#shoulder-height").property("disabled", !enabled);
  d3.select("#shoulder-offset").property("disabled", !enabled);
  d3.select("#shoulder-distance").property("disabled", !enabled);
}

let last_control_arm = localStorage.getItem("control-arm");
if (last_control_arm != null) {
  let enabled = JSON.parse(last_control_arm);
  d3.select("#control-arm").property("checked", enabled);
  set_arm_control(enabled);
} else {
  set_arm_control(false);
}

d3.select("#control-arm").on("change", ()=> {
  let enabled = d3.select("#control-arm").property("checked");
  localStorage.setItem("control-arm", JSON.stringify(enabled));
  set_arm_control(enabled);
})

let last_forearm_length = localStorage.getItem("forearm-length");
if (last_forearm_length != null) forearm_length = JSON.parse(last_forearm_length);
d3.select("#forearm-length-no").text(`${forearm_length}mm`);

d3.select("#forearm-length")
  .property("value", forearm_length)
  .on("change", function(){
    forearm_length = this.value;
    d3.select("#forearm-length-no").text(`${forearm_length}mm`);
    localStorage.setItem("forearm-length", JSON.stringify(forearm_length));
  });


let last_shoulder_height = localStorage.getItem("shoulder-height");
if (last_shoulder_height != null) shoulder_height = JSON.parse(last_shoulder_height);
d3.select("#shoulder-height-no").text(`${shoulder_height}mm`);

d3.select("#shoulder-height")
  .property("value", shoulder_height)
  .on("change", function(){
    shoulder_height = this.value;
    d3.select("#shoulder-height-no").text(`${shoulder_height}mm`);
    localStorage.setItem("shoulder-height", JSON.stringify(shoulder_height));
  });

let last_shoulder_offset = localStorage.getItem("shoulder-offset");
if (last_shoulder_offset != null) shoulder_offset = JSON.parse(last_shoulder_offset);
d3.select("#shoulder-offset-no").text(`${shoulder_offset}mm`);

d3.select("#shoulder-offset")
  .property("value", shoulder_offset)
  .on("change", function(){
    shoulder_offset = this.value;
    d3.select("#shoulder-offset-no").text(`${shoulder_offset}mm`);
    localStorage.setItem("shoulder-offset", JSON.stringify(shoulder_offset));
  });


let last_shoulder_distance = localStorage.getItem("shoulder-distance");
if (last_shoulder_distance != null) shoulder_distance = JSON.parse(last_shoulder_distance);
d3.select("#shoulder-distance-no").text(`${shoulder_distance}mm`);

d3.select("#shoulder-distance")
  .property("value", shoulder_distance)
  .on("change", function(){
    shoulder_distance = this.value;
    d3.select("#shoulder-distance-no").text(`${shoulder_distance}mm`);
    localStorage.setItem("shoulder-distance", JSON.stringify(shoulder_distance));
  });


// ### Remote commands

d3.select("body")
  .on("keydown", () => {
    if (d3.event.code == "Space") {
      driver.command();
      d3.select("#command_flag").text("Commanding").style("color", "orangered");
    }
  })
  .on("keyup", () => {
    if (d3.event.code == "Space"){
      driver.release();
      d3.select("#command_flag").text("Released").style("color", null);
    }
  });

function degrees(radians){
  if (radians == undefined) return "-";
  return (radians * 180 / Math.PI).toFixed(0);
}

// ### Remote state

function get_info(channel) {
  // Get the command we're about to send.
  let seek_to_send = driver.commands.seek[channel];
  let formatted_seek = seek_to_send == null ? "-" : seek_to_send.toFixed(2);

  // Skip info if we don't have state and config.
  if (driver.state == null || driver.config == null) {
    return [formatted_seek, "-", "-", "-"];
  }


  // Get measured position as fraction between min and max.
  let position = deinterpolate(
    driver.state.motor_channels[channel].position,
    driver.config.motor_channels[channel].min_position,
    driver.config.motor_channels[channel].max_position);

  // Get the PWM rate and current currently being sent to the motors.
  let driving_power = driver.state.motor_channels[channel].power;
  let driving_current = driver.state.motor_channels[channel].current;

  return [
    formatted_seek,
    position.toFixed(2),
    driving_power.toFixed(2),
    driving_current.toFixed(3)];
}



function show_joint_info(){
  // Skip if the hand isn't valid.
  if (!hand_state.valid) return;

  let {thumb, index, middle, ring, pinky} = hand_state.digits;

  let joint_info = [
    ["Wrist Roll", degrees(hand_state.palm_roll), ...get_info(channels.wrist_roll)],
    ["Wrist Yaw", degrees(hand_state.palm_yaw), ...get_info(channels.wrist_yaw)],
    ["Wrist Pitch", degrees(hand_state.palm_pitch), ...get_info(channels.wrist_pitch)],
    ["Thumb Pivot", degrees(hand_state.thumb_pivot), ...get_info(channels.thumb_pivot)],
    ["Pinky Pivot", degrees(hand_state.pinky_pivot), ...get_info(channels.pinky_pivot)],

    ["Pinky Yaw", degrees(pinky.yaw), ...get_info(channels.pinky_yaw)],
    ["Pinky Pitch", degrees(pinky.pitch), ...get_info(channels.pinky_pitch)],
    ["Pinky Curl", degrees(pinky.curl), ...get_info(channels.pinky_curl)],

    ["Ring Yaw", degrees(ring.yaw), ...get_info(channels.ring_yaw)],
    ["Ring Pitch", degrees(ring.pitch), ...get_info(channels.ring_pitch)],
    ["Ring Curl", degrees(ring.curl), ...get_info(channels.ring_curl)],

    ["Middle Yaw", degrees(middle.yaw), ...get_info(channels.middle_yaw)],
    ["Middle Pitch", degrees(middle.pitch), ...get_info(channels.middle_pitch)],
    ["Middle Curl", degrees(middle.curl), ...get_info(channels.middle_curl)],

    ["Index Yaw", degrees(index.yaw), ...get_info(channels.index_yaw)],
    ["Index Pitch", degrees(index.pitch), ...get_info(channels.index_pitch)],
    ["Index Curl", degrees(index.curl), ...get_info(channels.index_curl)],

    ["Thumb Yaw", degrees(thumb.yaw), ...get_info(channels.thumb_yaw)],
    ["Thumb Pitch", degrees(thumb.pitch), ...get_info(channels.thumb_pitch)],
    ["Thumb Curl", degrees(thumb.curl), ...get_info(channels.thumb_curl)],

    ["Elbow bend", degrees(hand_state.elbow_angle), ... get_info(channels.elbow_bend)],
    ["Arm twist", degrees(hand_state.arm_twist), ... get_info(channels.arm_twist)],
    ["Arm tilt/front gear", degrees(hand_state.arm_tilt), ... get_info(channels.arm_front)],
    ["Arm pivot/back gear", degrees(hand_state.arm_pivot), ... get_info(channels.arm_back)],
  ];

  d3.select("#joint_info")
    .selectAll("tr")
    .data(joint_info)
    .join("tr")
      .selectAll("td")
      .data(row => row)
      .join("td")
        .text(value => value);
}

function update_ui() {
  requestAnimationFrame(update_ui);
  show_joint_info();
}

update_ui();