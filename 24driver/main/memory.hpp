#pragma once

#include "web.hpp"

#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <stdio.h>

namespace memory {

  // Maximum key length on the esp32 nvs lib.
  const size_t MAX_KEY = 15;

  nvs_handle config_handle = 0;

  // TODO: report nvs error on screen?
  esp_err_t err = ESP_OK;

  void setup() {
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


  void save_wifi() {
    err = nvs_set_str(config_handle, "router_ssid", web::router_ssid);
    if (err != ESP_OK) return;

    err = nvs_set_str(config_handle, "router_pass", web::router_password);
    if (err != ESP_OK) return;

    err = nvs_set_str(config_handle, "ap_ssid", web::ap_ssid);
    if (err != ESP_OK) return;

    err = nvs_set_str(config_handle, "ap_pass", web::ap_password);
    if (err != ESP_OK) return;

    err = nvs_set_u8(config_handle, "conn_router", web::connect_to_router);
    if (err != ESP_OK) return;

    // Commit changes if all went well.
    err = nvs_commit(config_handle);
  }

  void save() {

    // char key[MAX_KEY+1];
    // snprintf(key, MAX_KEY+1, "j%2d-chip", i);

  }

  void load_wifi() {

    size_t length;

    length = web::MAX_LENGTH;
    err = nvs_get_str(config_handle, "router_ssid", web::router_ssid, &length);
    if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else return;

    length = web::MAX_LENGTH;
    err = nvs_get_str(config_handle, "router_pass", web::router_password, &length);
    if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else return;

    length = web::MAX_LENGTH;
    err = nvs_get_str(config_handle, "ap_ssid", web::ap_ssid, &length);
    if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else return;

    length = web::MAX_LENGTH;
    err = nvs_get_str(config_handle, "ap_pass", web::ap_password, &length);
    if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else return;

    uint8_t connect_to_router = web::connect_to_router;
    err = nvs_get_u8(config_handle, "conn_router", &connect_to_router);
    web::connect_to_router = connect_to_router;
    if (err == ESP_OK or err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else return;
  }



  void load(){
    load_wifi();
  }
}