#pragma once

#include "boot_clock.hpp"

#include <cstdint>

#if defined(ESP_PLATFORM)
#include "esp_event.h"
#endif

namespace frost {

// These are the only ESP-IDF lifecycle signals that the boot-clock gate
// consumes. A station being associated is deliberately not enough: SNTP and
// DNS need a configured IP interface, represented by STA_GOT_IP.
enum class EspIdfBootClockSignal : std::uint8_t {
  EventLoopCreationSucceeded,
  EventLoopCreationFailed,
  StationGotIp,
  StationLostIp,
  StationDisconnected,
  StationConnected,
  Unrelated,
};

// Narrow seam around ESP-IDF event-handler registration. The platform adapter
// translates the three signals below from ESP-IDF event-base/event-id pairs;
// host tests use a recording implementation to exercise ownership and cleanup.
class EspIdfBootClockEventRegistry {
 public:
  using Callback = void (*)(void*, EspIdfBootClockSignal);

  virtual ~EspIdfBootClockEventRegistry() = default;

  virtual bool register_handler(
      EspIdfBootClockSignal signal,
      Callback callback,
      void* context) = 0;
  virtual bool unregister_handler(
      EspIdfBootClockSignal signal,
      Callback callback,
      void* context) = 0;
};

#if defined(ESP_PLATFORM)
// Concrete ESP-IDF adapter for the narrow registry seam above. It owns only
// the raw event callback and registration bookkeeping; the bridge owns the
// callback context and decides what each abstract signal means.
class EspIdfBootClockEventRegistryAdapter final
    : public EspIdfBootClockEventRegistry {
 public:
  bool register_handler(
      EspIdfBootClockSignal signal,
      Callback callback,
      void* context) override;
  bool unregister_handler(
      EspIdfBootClockSignal signal,
      Callback callback,
      void* context) override;

 private:
  static void on_event(
      void* event_handler_arg,
      esp_event_base_t event_base,
      std::int32_t event_id,
      void* event_data);

  static bool event_for_signal(
      EspIdfBootClockSignal signal,
      esp_event_base_t* event_base,
      std::int32_t* event_id);
  static EspIdfBootClockSignal signal_for_event(
      esp_event_base_t event_base,
      std::int32_t event_id);
  static std::uint8_t bit_for_signal(EspIdfBootClockSignal signal);

  Callback callback_ = nullptr;
  void* context_ = nullptr;
  std::uint8_t registered_handler_mask_ = 0;
};
#endif

enum class EspIdfBootClockRegistrationResult : std::uint8_t {
  Registered,
  AlreadyRegistered,
  RegistrationFailed,
  CleanupFailed,
  Unregistered,
  NotRegistered,
};

// Maps ESP-IDF setup/event callbacks to the platform-neutral prerequisite
// gate. The bridge owns registration through the narrow registry seam and
// unregisters handlers on teardown. It does not start Wi-Fi, SNTP, or
// autonomous scheduling.
class EspIdfBootClockEventBridge final {
 public:
  explicit EspIdfBootClockEventBridge(
      EspIdfBootClockPrerequisites& prerequisites,
      EspIdfBootClockEventRegistry& registry);
  ~EspIdfBootClockEventBridge();

  EspIdfBootClockRegistrationResult register_handlers();
  EspIdfBootClockRegistrationResult unregister_handlers();

  void report_event_loop_creation(bool succeeded);
  void handle(EspIdfBootClockSignal signal);

 private:
  static void on_registered_event(
      void* context,
      EspIdfBootClockSignal signal);

  EspIdfBootClockPrerequisites& prerequisites_;
  EspIdfBootClockEventRegistry& registry_;
  std::uint8_t registered_handler_mask_ = 0;
};

}  // namespace frost
