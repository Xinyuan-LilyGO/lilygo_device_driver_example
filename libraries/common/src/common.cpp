/*
 * @Description: None
 * @Author: LILYGO_L
 * @Date: 2026-07-11 16:22:23
 * @LastEditTime: 2026-07-11 16:23:17
 * @License: GPL 3.0
 */
#include "common.h"

#if defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE)
#include "esp_err.h"
#include "esp_hosted_transport_config.h"

namespace {

esp_err_t SetWifiCoprocessorResetLevel(void*, bool level) {
  return common::SetWifiCoprocessorPowerEnabled(level) ? ESP_OK : ESP_FAIL;
}

}  // namespace
#endif

namespace common {

bool RegisterWifiCoprocessorResetCallback() {
#if defined(CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE)
  return esp_hosted_sdio_set_reset_callback(
             SetWifiCoprocessorResetLevel, nullptr) == ESP_TRANSPORT_OK;
#else
  return false;
#endif
}

}  // namespace common
