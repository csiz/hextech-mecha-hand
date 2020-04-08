"use strict";

import * as d3 from "d3";
import * as THREE from "three";

import {version} from "24driver";


// Hand data
// ---------



function make_default_hand_state () {
  const default_digit_state = {
    distal_tip: null,
    distal_basis: null,

    intermediate_tip: null,
    intermediate_basis: null,

    proximal_tip: null,
    proximal_basis: null,

    metacarpal_tip: null,
    metacarpal_basis: null,
  };

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

    palm_pitch: null,
    palm_roll: null,
    palm_yaw: null,


    digits: {
      thumb: {...default_digit_state},
      index: {... default_digit_state},
      middle: {... default_digit_state},
      ring: {... default_digit_state},
      pinkie: {... default_digit_state},
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
  const palm_roll = signed_angle_on_plane(normal, new THREE.Vector3(0, 1, 0), direction);

  // Compute the tranform that switches coordinates to the arm perspective. However,
  // we keep the roll, since it's value is derived from the direction of gravity.
  const local_transform = arm_local_transform.clone()
    .premultiply(new THREE.Matrix4().makeRotationZ(palm_roll))
    .setPosition(palm_world_position.clone().negate());


  // Get the palm basis, in the reference frame of the arm.
  const palm_basis = palm_world_basis.clone().premultiply(local_transform);

  // Get the remaining Euler angles for the wrist/palm. The physical model of the hand
  // applies roll (Z) followed by yaw (Y) and finally pitch (X); use the same axis order.
  const palm_angles = new THREE.Euler().setFromRotationMatrix(palm_basis, "ZYX");
  const palm_yaw = -palm_angles.y;
  const palm_pitch = -palm_angles.x;
  // We should already have palm roll with respect to gravity. There is a small discrepancy
  // in the leap controller between the roll for the arm and the roll for the hand. But it
  // seems like we're really close; agreeing up to 2 decimal places in degrees.

  return {
    valid: true,
    leap_hand_id,
    local_transform,
    arm_world_basis,
    palm_world_basis,
    palm_basis,
    palm_world_position,
    palm_pitch,
    palm_roll,
    palm_yaw,
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
const camera_size = 1000;
let camera = new THREE.OrthographicCamera(-0.5*ratio*camera_size, +0.5*ratio*camera_size, +0.5*camera_size, -0.5*camera_size, 0.0, 2000);

let renderer = new THREE.WebGLRenderer({antialias: true, alpha: true});
renderer.setSize(width, height);

d3.select("main").append(() => renderer.domElement);



let scene = new THREE.Scene();

let cube_geometry = new THREE.BoxGeometry(200, 20, 200);
let cube_material = new THREE.MeshBasicMaterial({color: 0x4040D0});
let cube = new THREE.Mesh(cube_geometry, cube_material);

scene.add(cube);

let cube_edges_geometry = new THREE.EdgesGeometry(cube.geometry);
let cube_edges_material = new THREE.LineBasicMaterial({color: 0x000000});
let cube_edges = new THREE.LineSegments(cube_edges_geometry, cube_edges_material);
cube_edges.renderOrder = 1;

cube.add(cube_edges);


camera.position.z = 1000;
camera.position.y = 300;

let animate = () => {
  requestAnimationFrame(animate);

  if (hand_state.valid) {
    cube.position.copy(hand_state.palm_world_position);
    cube.quaternion.setFromRotationMatrix(hand_state.palm_basis);
  }

  renderer.render(scene, camera);
};

animate();