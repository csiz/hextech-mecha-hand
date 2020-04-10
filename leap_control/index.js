"use strict";

import * as d3 from "d3";
import * as THREE from "three";

import MotorDriver from "24driver";

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

    arm_world_basis: null,

    palm_world_basis: null,

    palm_basis: null,

    palm_world_position: null,

    palm_position: null,

    palm_pitch: null,
    palm_roll: null,
    palm_yaw: null,


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

/* Convenince function to extract the basis into 3 vectors. */
function get_basis_vectors(m4) {
  let x_axis = new Vector3();
  let y_axis = new Vector3();
  let z_axis = new Vector3();
  m4.extractBasis(x_axis, y_axis, z_axis);
  return {x_axis, y_axis, z_axis}
}

function parse_hand_state(leap_state){
  // No hands in view, return invalid state.
  if (leap_state.hands === undefined || leap_state.hands.length < 1) return make_default_hand_state();

  const leap_hand = leap_state.hands[0];

  // Only accept right hand for now. Sometimes it detects it as a left hand, but with messed up finger
  // positions. We can avoid some spazzing by requiring right hand strictly.
  if (leap_hand.type != "right") return make_default_hand_state();

  // Remember the hand id, we need it to match the fingers.
  const leap_hand_id = leap_hand.id;

  // We'll use the basis of the arm as
  const arm_world_basis = new THREE.Matrix4().makeBasis(
    ...leap_hand.armBasis.map(vec => new THREE.Vector3(...vec))
  );

  // Because the matrix is orthonormal, we can transpose it to get the inverse.
  const arm_local_transform = arm_world_basis.clone().transpose();

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
  const local_transform = arm_local_transform.clone()
  .premultiply(new THREE.Matrix4().makeRotationZ(roll))
  .setPosition(palm_world_position.clone().negate());


  // Get the palm basis, in the reference frame of the arm.
  const palm_basis = palm_world_basis.clone().premultiply(local_transform);
  const palm_position = palm_world_position.clone().applyMatrix4(local_transform);

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


  return {
    valid: true,
    leap_hand_id,
    local_transform,
    arm_world_basis,
    palm_world_basis,
    palm_basis,
    palm_world_position,
    palm_position,
    palm_pitch,
    palm_roll,
    palm_yaw,

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

const width = 1600;
const height = 900;
const ratio = width / height;

// let camera = new THREE.PerspectiveCamera(90, ratio, 0.1, 1000);
const camera_size = 500;
let camera = new THREE.OrthographicCamera(-0.5*ratio*camera_size, +0.5*ratio*camera_size, +0.5*camera_size, -0.5*camera_size, 0.0, 2000);

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

  return {cube, cube_edges};
};

let {cube: palm_cube, cube_edges: palm_cube_edges} = make_edgy_cube(40, 5, 40, "orangered", edge_color);

scene.add(palm_cube);
palm_cube.add(palm_cube_edges);


let digit_models = ["thumb", "index", "middle", "ring", "pinky"].map(digit_name => {

  let {cube: knuckle_cube, cube_edges: knuckle_cube_edges} = make_edgy_cube(20, 20, 20, main_color, edge_color);
  scene.add(knuckle_cube);
  knuckle_cube.add(knuckle_cube_edges);

  let {cube: curl_cube, cube_edges: curl_cube_edges} = make_edgy_cube(20, 20, 20, main_color, edge_color);
  scene.add(curl_cube);
  curl_cube.add(curl_cube_edges);

  let {cube: distal_cube, cube_edges: distal_cube_edges} = make_edgy_cube(10, 10, 10, main_color, edge_color);
  scene.add(distal_cube);
  distal_cube.add(distal_cube_edges);

  let {cube: tip_cube, cube_edges: tip_cube_edges} = make_edgy_cube(20, 20, 20, main_color, edge_color);
  scene.add(tip_cube);
  tip_cube.add(tip_cube_edges);

  return {
    name: digit_name,
    knuckle_cube, knuckle_cube_edges,
    curl_cube, curl_cube_edges,
    distal_cube, distal_cube_edges,
    tip_cube, tip_cube_edges,
  };
});

camera.position.z = 500;
camera.position.y = 300;
camera.lookAt(0, 0, 0);


let animate = () => {
  requestAnimationFrame(animate);

  if (hand_state.valid) {
    palm_cube.position.copy(hand_state.palm_position);
    palm_cube.quaternion.setFromRotationMatrix(hand_state.palm_basis);

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
  }

  renderer.render(scene, camera);
};

animate();



// Motor Driver
// ------------

let channels = {
  wrist_roll: 23,
  wrist_yaw: 19,
  wrist_pitch: 15,
};

let driver = new MotorDriver();

driver.onsendcommands = function(){
  if (!hand_state.valid) return;

  driver.commands.seek[channels.wrist_roll] = -hand_state.palm_roll / (0.8 * Math.PI) + 0.5;
  driver.commands.seek[channels.wrist_yaw] = hand_state.palm_yaw / (0.2 * Math.PI) + 0.5;
  driver.commands.seek[channels.wrist_pitch] = hand_state.palm_pitch / (0.5 * Math.PI) + 0.5;

  console.log(driver.commands);
}

// UI
// --

d3.select("#driver_connect").on("click", () => {
  let ip = d3.select("#driver_ip").property("value");
  driver.url = `ws://${ip}/ws`;
  driver.connect();
});


d3.select("body")
  .on("keydown", () => { if (d3.event.code == "Space") driver.command(); })
  .on("keyup", () => { if (d3.event.code == "Space") driver.release(); });
