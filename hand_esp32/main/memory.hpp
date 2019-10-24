#pragma once

#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"


#include "joints.hpp"
#include "power.hpp"

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

    // TODO: save joint config.
  }
  void load() {

    // Load power scales.
    err = nvs_get_i32(config_handle, "voltage_scale", &power::voltage_scale);
    if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else return;

    err = nvs_get_i32(config_handle, "current_scale", &power::current_scale);
    if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else return;

    // TODO: load joint config.
  }


  void close(){
    if (err == ESP_OK) nvs_close(config_handle);
  }

}