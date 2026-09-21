#pragma once
#include "esp_event.h"
extern const esp_event_base_t NETIF_SNTP_EVENT;
constexpr int32_t NETIF_SNTP_TIME_SYNC = 2;
struct esp_sntp_config_t {
  const char* server;
  bool start = true;
  bool wait_for_sync = true;
  void (*sync_cb)(void*) = nullptr;
};
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) esp_sntp_config_t{server}
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t*);
esp_err_t esp_netif_sntp_start();
void esp_netif_sntp_deinit();
