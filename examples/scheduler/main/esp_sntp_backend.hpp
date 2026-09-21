#pragma once

#include "clock.hpp"

#include "esp_event_base.h"

namespace frost {

// ESP-IDF binding for the platform-neutral SNTP source. It registers the
// esp-netif synchronization event, starts SNTP, and reads the system UTC
// clock. Wi-Fi setup and autonomous scheduling remain outside this class.
class EspIdfSntpBackend final : public NetworkTimeSourceBackend {
 public:
  NetworkTimeBackendResult register_sync_handler(
      SyncCallback callback,
      void* context) override;
  NetworkTimeBackendResult initialize_sntp(
      const char* server) override;
  NetworkTimeBackendResult start_sntp() override;
  void unregister_sync_handler() override;
  void deinitialize_sntp() override;
  std::time_t utc_now() const override;

 private:
  static void on_network_ready(
      void* event_handler_arg,
      esp_event_base_t event_base,
      int32_t event_id,
      void* event_data);

  static void on_sntp_event(
      void* event_handler_arg,
      esp_event_base_t event_base,
      int32_t event_id,
      void* event_data);

  SyncCallback sync_callback_ = nullptr;
  void* sync_context_ = nullptr;
  bool handler_registered_ = false;
  bool sntp_initialized_ = false;
};

}  // namespace frost
