#include "esp_sntp_backend.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

#include <ctime>

namespace frost {

NetworkTimeBackendResult EspIdfSntpBackend::register_sync_handler(
    SyncCallback callback,
    void* context) {
  if (callback == nullptr) {
    return NetworkTimeBackendResult::Error;
  }

  const esp_err_t result = esp_event_handler_register(
      NETIF_SNTP_EVENT,
      NETIF_SNTP_TIME_SYNC,
      &EspIdfSntpBackend::on_sntp_event,
      this);
  if (result != ESP_OK) {
    return NetworkTimeBackendResult::Error;
  }

  sync_callback_ = callback;
  sync_context_ = context;
  handler_registered_ = true;
  return NetworkTimeBackendResult::Ok;
}

NetworkTimeBackendResult EspIdfSntpBackend::initialize_sntp(
    const char* server) {
  if (server == nullptr || *server == '\0') {
    return NetworkTimeBackendResult::Error;
  }

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
  config.start = false;
  config.wait_for_sync = false;
  config.sync_cb = nullptr;

  const esp_err_t result = esp_netif_sntp_init(&config);
  if (result != ESP_OK) {
    return NetworkTimeBackendResult::Error;
  }

  // Handle IP recovery in the event loop, including while BLE blocks the
  // runtime task. A refresh requests time; it never revokes existing trust.
  if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                 &EspIdfSntpBackend::on_network_ready,
                                 this) != ESP_OK) {
    esp_netif_sntp_deinit();
    return NetworkTimeBackendResult::Error;
  }
  sntp_initialized_ = true;
  return NetworkTimeBackendResult::Ok;
}

NetworkTimeBackendResult EspIdfSntpBackend::start_sntp() {
  if (esp_netif_sntp_start() != ESP_OK) {
    return NetworkTimeBackendResult::Error;
  }
  return NetworkTimeBackendResult::Ok;
}

void EspIdfSntpBackend::unregister_sync_handler() {
  if (!handler_registered_) {
    return;
  }

  if (esp_event_handler_unregister(
          NETIF_SNTP_EVENT,
          NETIF_SNTP_TIME_SYNC,
          &EspIdfSntpBackend::on_sntp_event) == ESP_OK) {
    handler_registered_ = false;
    sync_callback_ = nullptr;
    sync_context_ = nullptr;
  }
}

void EspIdfSntpBackend::deinitialize_sntp() {
  if (!sntp_initialized_) {
    return;
  }

  (void)esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &EspIdfSntpBackend::on_network_ready);
  esp_netif_sntp_deinit();
  sntp_initialized_ = false;
}

void EspIdfSntpBackend::on_network_ready(
    void* event_handler_arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data) {
  (void)event_data;
  auto* backend = static_cast<EspIdfSntpBackend*>(event_handler_arg);
  if (backend == nullptr || event_base != IP_EVENT ||
      event_id != IP_EVENT_STA_GOT_IP) {
    return;
  }
  if (backend->start_sntp() == NetworkTimeBackendResult::Ok) {
    ESP_LOGI("frost_clock", "time refresh requested after IP readiness");
  } else {
    ESP_LOGW("frost_clock", "time refresh request failed; existing clock retained");
  }
}

std::time_t EspIdfSntpBackend::utc_now() const {
  std::time_t now = 0;
  time(&now);
  return now;
}

void EspIdfSntpBackend::on_sntp_event(
    void* event_handler_arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void* event_data) {
  (void)event_data;
  auto* backend = static_cast<EspIdfSntpBackend*>(event_handler_arg);
  if (backend == nullptr || event_base != NETIF_SNTP_EVENT ||
      event_id != NETIF_SNTP_TIME_SYNC || backend->sync_callback_ == nullptr) {
    return;
  }

  backend->sync_callback_(backend->sync_context_);
}

}  // namespace frost
