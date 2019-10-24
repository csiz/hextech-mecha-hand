#pragma once

#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"


#include "joints.hpp"
#include "power.hpp"

// Maximum key length on the esp32 nvs lib.
#define MAX_KEY 15

namespace memory {

  nvs_handle config_handle = 0;

  esp_err_t err = ESP_OK;

  void init() {
    // Bunch of init code copied from: https://github.com/espressif/esp-idf/blob/2e6398affaeeac2f7ce40457a881f2dda57ad11f/examples/storage/nvs_rw_value/main/nvs_value_example_main.c

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES or err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return;


    // Open the config namespace for read and write.
    err = nvs_open("config", NVS_READWRITE, &config_handle);

    if (err != ESP_OK) return;
  }


  void save() {
    // Save power scales.
    err = nvs_set_i32(config_handle, "voltage_scale", power::voltage_scale);
    if (err != ESP_OK) return;

    err = nvs_set_i32(config_handle, "current_scale", power::current_scale);
    if (err != ESP_OK) return;


    // Save joints config.
    using namespace joints;
    using joints::joints; // To remove ambiguity with namespace.

    char key[MAX_KEY+1];

    for (int i = 0; i < NUM_JOINTS; i++){
      Joint const& joint = joints[i];

      // Write the value if it's different from the default, otherwise erase the value and leave default.
      snprintf(key, MAX_KEY+1, "j%2d-chip", i);
      if (joint.chip != default_joint.chip) {
        err = nvs_set_i8(config_handle, key, typed(joint.chip));
      } else {
        err = nvs_erase_key(config_handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      }
      if (err != ESP_OK) return;

      snprintf(key, MAX_KEY+1, "j%2d-in-idx", i);
      if (joint.input_index != default_joint.input_index) {
        err = nvs_set_i8(config_handle, key, joint.input_index);
      } else {
        err = nvs_erase_key(config_handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      }
      if (err != ESP_OK) return;

      snprintf(key, MAX_KEY+1, "j%2d-out-idx", i);
      if (joint.output_index != default_joint.output_index) {
        err = nvs_set_i8(config_handle, key, joint.output_index);
      } else {
        err = nvs_erase_key(config_handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      }
      if (err != ESP_OK) return;

      snprintf(key, MAX_KEY+1, "j%2d-min-pos", i);
      if (joint.min_pos != default_joint.min_pos) {
        err = nvs_set_i16(config_handle, key, joint.min_pos);
      } else {
        err = nvs_erase_key(config_handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      }
      if (err != ESP_OK) return;

      snprintf(key, MAX_KEY+1, "j%2d-max-pos", i);
      if (joint.max_pos != default_joint.max_pos) {
        err = nvs_set_i16(config_handle, key, joint.max_pos);
      } else {
        err = nvs_erase_key(config_handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      }
      if (err != ESP_OK) return;

      snprintf(key, MAX_KEY+1, "j%2d-inv-pos", i);
      if (joint.inverted_position != default_joint.inverted_position) {
        err = nvs_set_i8(config_handle, key, joint.inverted_position);
      } else {
        err = nvs_erase_key(config_handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      }
      if (err != ESP_OK) return;

      snprintf(key, MAX_KEY+1, "j%2d-inv-out", i);
      if (joint.inverted_output != default_joint.inverted_output) {
        err = nvs_set_i8(config_handle, key, joint.inverted_output);
      } else {
        err = nvs_erase_key(config_handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      }
      if (err != ESP_OK) return;
    }

    // Commit changes if all went well.
    err = nvs_commit(config_handle);
  }
  void load() {

    // Load power scales.
    err = nvs_get_i32(config_handle, "voltage_scale", &power::voltage_scale);
    if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else return;

    err = nvs_get_i32(config_handle, "current_scale", &power::current_scale);
    if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else return;

    // Load joints config.
    using namespace joints;
    using joints::joints; // To remove ambiguity with namespace.

    char key[MAX_KEY+1];

    for (int i = 0; i < NUM_JOINTS; i++){
      Joint & joint = joints[i];

      snprintf(key, MAX_KEY+1, "j%2d-chip", i);
      int8_t chip_enum = typed(default_joint.chip);
      err = nvs_get_i8(config_handle, key, &chip_enum);
      joint.chip = static_cast<Chip>(chip_enum);
      if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      else return;

      snprintf(key, MAX_KEY+1, "j%2d-in-idx", i);
      int8_t input_index = default_joint.input_index;
      err = nvs_get_i8(config_handle, key, &input_index);
      joint.input_index = input_index;
      if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      else return;

      snprintf(key, MAX_KEY+1, "j%2d-out-idx", i);
      int8_t output_index = default_joint.output_index;
      err = nvs_get_i8(config_handle, key, &output_index);
      joint.output_index = output_index;
      if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      else return;

      snprintf(key, MAX_KEY+1, "j%2d-min-pos", i);
      int16_t min_pos = default_joint.min_pos;
      err = nvs_get_i16(config_handle, key, &min_pos);
      joint.min_pos = min_pos;
      if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      else return;

      snprintf(key, MAX_KEY+1, "j%2d-max-pos", i);
      int16_t max_pos = default_joint.max_pos;
      err = nvs_get_i16(config_handle, key, &max_pos);
      joint.max_pos = max_pos;
      if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      else return;


      snprintf(key, MAX_KEY+1, "j%2d-inv-pos", i);
      int8_t inverted_position = default_joint.inverted_position;
      err = nvs_get_i8(config_handle, key, &inverted_position);
      joint.inverted_position = inverted_position;
      if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      else return;

      snprintf(key, MAX_KEY+1, "j%2d-inv-out", i);
      int8_t inverted_output = default_joint.inverted_output;
      err = nvs_get_i8(config_handle, key, &inverted_output);
      joint.inverted_output = inverted_output;
      if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
      else return;
    }
  }

  void reset_defaults(){
    // Reset power config.
    using namespace power;
    voltage_scale = default_voltage_scale;
    current_scale = default_current_scale;

    // Reset joints to defaults.
    using namespace joints;
    using joints::joints;

    for (int i = 0; i < NUM_JOINTS; i++){
      joints[i] = default_joint;
    }
  }


  void close(){
    if (err == ESP_OK) nvs_close(config_handle);
  }

}