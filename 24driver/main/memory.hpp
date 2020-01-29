#pragma once

#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"


namespace memory {

  // Maximum key length on the esp32 nvs lib, and spot for the \0 terminator.
  const size_t max_key = 15 + 1;

  // Handle to the open storage.
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


  inline void set_str(const char * key, const char * str) {
    // Save key & value pair only if memory is in ok state.
    if (err != ESP_OK) return;
    err = nvs_set_str(config_handle, key, str);
  }

  inline void get_str(const char * key, char * str, size_t max_length) {
    // Load key & value pair only if memory is in ok state.
    if (err != ESP_OK) return;
    auto get_err = nvs_get_str(config_handle, key, str, &max_length);
    // Silently ignore not finding the key; str remains the default value.
    if (get_err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    // Othewise set to the error returned (or ESP_OK).
    else err = get_err;
  }

  inline void set_u8(const char * key, uint8_t value) {
    if (err != ESP_OK) return;
    err = nvs_set_u8(config_handle, key, value);
  }

  inline void get_u8(const char * key, uint8_t & value) {
    if (err != ESP_OK) return;
    auto get_err = nvs_get_u8(config_handle, key, &value);
    if (get_err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else err = get_err;
  }

  inline void set_bool(const char * key, bool value) {
    set_u8(key, reinterpret_cast<uint8_t &>(value));
  }

  inline void get_bool(const char * key, bool & value) {
    get_u8(key, reinterpret_cast<uint8_t &>(value));
  }

  inline void set_u32(const char * key, uint32_t value) {
    if (err != ESP_OK) return;
    err = nvs_set_u32(config_handle, key, value);
  }

  inline void get_u32(const char * key, uint32_t & value) {
    if (err != ESP_OK) return;
    auto get_err = nvs_get_u32(config_handle, key, &value);
    if (get_err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    else err = get_err;
  }

  inline void set_float(const char * key, float value) {
    set_u32(key, reinterpret_cast<uint32_t &>(value));
  }

  inline void get_float(const char * key, float & value) {
    get_u32(key, reinterpret_cast<uint32_t &>(value));
  }


  inline void commit() {
    // Commit changes if all went well.
    if (err != ESP_OK) return;
    err = nvs_commit(config_handle);
  }

}