#include "esp_sntp_backend.hpp"
#include "esp_event.h"
#include "esp_netif_sntp.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

const esp_event_base_t IP_EVENT = "IP";
const esp_event_base_t NETIF_SNTP_EVENT = "SNTP";

namespace {
struct Handler {
  esp_event_base_t base;
  int32_t id;
  esp_event_handler_t callback;
  void* context;
};
std::vector<Handler> handlers;
bool fail_ip_registration = false;
esp_err_t start_result = ESP_OK;
int starts = 0;
bool initialized = false;

void emit(esp_event_base_t base, int32_t id) {
  for (const auto& handler : handlers) {
    if (handler.base == base && handler.id == id) {
      handler.callback(handler.context, base, id, nullptr);
    }
  }
}
}  // namespace

esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id,
                                     esp_event_handler_t callback, void* context) {
  if (base == IP_EVENT && fail_ip_registration) return -1;
  handlers.push_back({base, id, callback, context});
  return ESP_OK;
}

esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id,
                                       esp_event_handler_t callback) {
  handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
      [&](const Handler& h) {
        return h.base == base && h.id == id && h.callback == callback;
      }), handlers.end());
  return ESP_OK;
}

esp_err_t esp_netif_sntp_init(const esp_sntp_config_t* config) {
  assert(!initialized);
  assert(!config->start && !config->wait_for_sync && config->sync_cb == nullptr);
  assert(std::strcmp(config->server, "test.invalid") == 0);
  initialized = true;
  return ESP_OK;
}

esp_err_t esp_netif_sntp_start() {
  assert(initialized);
  ++starts;
  return start_result;
}

void esp_netif_sntp_deinit() {
  assert(initialized);
  // No IP callback may outlive the SNTP configuration.
  for (const auto& h : handlers) assert(h.base != IP_EVENT);
  initialized = false;
}

int main() {
  frost::EspIdfSntpBackend backend;
  frost::SntpNetworkTimeSource source(backend);
  assert(source.initialize("test.invalid") == frost::NetworkTimeSourceInitResult::Initialized);
  assert(starts == 1);
  assert(!source.is_synchronized());

  // Recovery before first sync requests time but does not create trust.
  emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
  assert(starts == 2);
  assert(!source.is_synchronized());
  emit(NETIF_SNTP_EVENT, NETIF_SNTP_TIME_SYNC);
  assert(source.is_synchronized());

  // IP events trigger requests without any polling of the runtime task.
  emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
  assert(starts == 3);
  assert(source.is_synchronized());
  start_result = -1;
  emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
  assert(starts == 4);
  assert(source.is_synchronized());  // Failed refresh retains running time.
  start_result = ESP_OK;
  emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
  assert(starts == 5);
  emit(NETIF_SNTP_EVENT, NETIF_SNTP_TIME_SYNC);
  assert(source.is_synchronized());
  backend.deinitialize_sntp();
  backend.unregister_sync_handler();
  assert(handlers.empty());
  emit(IP_EVENT, IP_EVENT_STA_GOT_IP);
  assert(starts == 5);

  // Initialization failure rolls back both the SNTP service and sync handler.
  fail_ip_registration = true;
  frost::EspIdfSntpBackend failed_backend;
  frost::SntpNetworkTimeSource failed_source(failed_backend);
  assert(failed_source.initialize("test.invalid") ==
         frost::NetworkTimeSourceInitResult::SntpInitializationFailed);
  assert(!failed_source.is_synchronized());
  assert(!initialized && handlers.empty());
  assert(starts == 5);

  fail_ip_registration = false;
  start_result = -1;
  assert(failed_source.initialize("test.invalid") ==
         frost::NetworkTimeSourceInitResult::SntpStartFailed);
  assert(!failed_source.is_synchronized());
  assert(!initialized && handlers.empty());
}
