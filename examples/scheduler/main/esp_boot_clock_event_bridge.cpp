#include "esp_boot_clock_event_bridge.hpp"

#if defined(ESP_PLATFORM)
#include "esp_netif.h"
#include "esp_wifi.h"
#if defined(FROST_WIFI_EVENT_SMOKE_TEST)
#include "esp_log.h"
#endif
#endif

#include <cstddef>

namespace frost {

namespace {

constexpr EspIdfBootClockSignal kRegisteredSignals[] = {
    EspIdfBootClockSignal::StationGotIp,
    EspIdfBootClockSignal::StationLostIp,
    EspIdfBootClockSignal::StationDisconnected,
};

constexpr std::uint8_t kAllRegisteredHandlersMask =
    static_cast<std::uint8_t>((1U << (sizeof(kRegisteredSignals) /
                                      sizeof(kRegisteredSignals[0]))) -
                              1U);

}  // namespace

#if defined(ESP_PLATFORM)

std::uint8_t EspIdfBootClockEventRegistryAdapter::bit_for_signal(
    EspIdfBootClockSignal signal) {
  switch (signal) {
    case EspIdfBootClockSignal::StationGotIp:
      return 1U << 0;
    case EspIdfBootClockSignal::StationLostIp:
      return 1U << 1;
    case EspIdfBootClockSignal::StationDisconnected:
      return 1U << 2;
    case EspIdfBootClockSignal::EventLoopCreationSucceeded:
    case EspIdfBootClockSignal::EventLoopCreationFailed:
    case EspIdfBootClockSignal::StationConnected:
    case EspIdfBootClockSignal::Unrelated:
      return 0;
  }
  return 0;
}

bool EspIdfBootClockEventRegistryAdapter::event_for_signal(
    EspIdfBootClockSignal signal,
    esp_event_base_t* event_base,
    std::int32_t* event_id) {
  if (event_base == nullptr || event_id == nullptr) {
    return false;
  }

  switch (signal) {
    case EspIdfBootClockSignal::StationGotIp:
      *event_base = IP_EVENT;
      *event_id = IP_EVENT_STA_GOT_IP;
      return true;
    case EspIdfBootClockSignal::StationLostIp:
      *event_base = IP_EVENT;
      *event_id = IP_EVENT_STA_LOST_IP;
      return true;
    case EspIdfBootClockSignal::StationDisconnected:
      *event_base = WIFI_EVENT;
      *event_id = WIFI_EVENT_STA_DISCONNECTED;
      return true;
    case EspIdfBootClockSignal::EventLoopCreationSucceeded:
    case EspIdfBootClockSignal::EventLoopCreationFailed:
    case EspIdfBootClockSignal::StationConnected:
    case EspIdfBootClockSignal::Unrelated:
      return false;
  }
  return false;
}

EspIdfBootClockSignal EspIdfBootClockEventRegistryAdapter::signal_for_event(
    esp_event_base_t event_base,
    std::int32_t event_id) {
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    return EspIdfBootClockSignal::StationGotIp;
  }
  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
    return EspIdfBootClockSignal::StationLostIp;
  }
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    return EspIdfBootClockSignal::StationDisconnected;
  }
  return EspIdfBootClockSignal::Unrelated;
}

bool EspIdfBootClockEventRegistryAdapter::register_handler(
    EspIdfBootClockSignal signal,
    Callback callback,
    void* context) {
  const std::uint8_t bit = bit_for_signal(signal);
  if (bit == 0 || callback == nullptr ||
      (registered_handler_mask_ != 0 &&
       (callback_ != callback || context_ != context)) ||
      (registered_handler_mask_ & bit) != 0) {
    return false;
  }

  esp_event_base_t event_base = nullptr;
  std::int32_t event_id = 0;
  if (!event_for_signal(signal, &event_base, &event_id) ||
      esp_event_handler_register(
          event_base, event_id, &EspIdfBootClockEventRegistryAdapter::on_event,
          this) != ESP_OK) {
    return false;
  }

  if (registered_handler_mask_ == 0) {
    callback_ = callback;
    context_ = context;
  }
  registered_handler_mask_ = static_cast<std::uint8_t>(
      registered_handler_mask_ | bit);
  return true;
}

bool EspIdfBootClockEventRegistryAdapter::unregister_handler(
    EspIdfBootClockSignal signal,
    Callback callback,
    void* context) {
  const std::uint8_t bit = bit_for_signal(signal);
  if (bit == 0 || callback == nullptr ||
      (registered_handler_mask_ & bit) == 0 || callback_ != callback ||
      context_ != context) {
    return false;
  }

  esp_event_base_t event_base = nullptr;
  std::int32_t event_id = 0;
  if (!event_for_signal(signal, &event_base, &event_id) ||
      esp_event_handler_unregister(
          event_base, event_id,
          &EspIdfBootClockEventRegistryAdapter::on_event) != ESP_OK) {
    return false;
  }

  registered_handler_mask_ = static_cast<std::uint8_t>(
      registered_handler_mask_ & static_cast<std::uint8_t>(~bit));
  if (registered_handler_mask_ == 0) {
    callback_ = nullptr;
    context_ = nullptr;
  }
  return true;
}

void EspIdfBootClockEventRegistryAdapter::on_event(
    void* event_handler_arg,
    esp_event_base_t event_base,
    std::int32_t event_id,
    void* event_data) {
  (void)event_data;
  auto* adapter =
      static_cast<EspIdfBootClockEventRegistryAdapter*>(event_handler_arg);
  if (adapter == nullptr || adapter->callback_ == nullptr) {
    return;
  }

#if defined(FROST_WIFI_EVENT_SMOKE_TEST)
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto* disconnected =
        static_cast<const wifi_event_sta_disconnected_t*>(event_data);
    ESP_LOGI(
        "scheduler_frost",
        "wifi smoke raw WIFI_EVENT_STA_DISCONNECTED reason=%u",
        disconnected == nullptr
            ? 0U
            : static_cast<unsigned>(disconnected->reason));
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI("scheduler_frost", "wifi smoke raw IP_EVENT_STA_GOT_IP");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_LOST_IP) {
    ESP_LOGI("scheduler_frost", "wifi smoke raw IP_EVENT_STA_LOST_IP");
  }
#endif

  const EspIdfBootClockSignal signal =
      signal_for_event(event_base, event_id);
  if (signal != EspIdfBootClockSignal::Unrelated) {
    adapter->callback_(adapter->context_, signal);
  }
}

#endif

EspIdfBootClockEventBridge::EspIdfBootClockEventBridge(
    EspIdfBootClockPrerequisites& prerequisites,
    EspIdfBootClockEventRegistry& registry)
    : prerequisites_(prerequisites), registry_(registry) {}

EspIdfBootClockEventBridge::~EspIdfBootClockEventBridge() {
  (void)unregister_handlers();
}

EspIdfBootClockRegistrationResult
EspIdfBootClockEventBridge::register_handlers() {
  if (registered_handler_mask_ == kAllRegisteredHandlersMask) {
    return EspIdfBootClockRegistrationResult::AlreadyRegistered;
  }
  if (registered_handler_mask_ != 0) {
    return EspIdfBootClockRegistrationResult::CleanupFailed;
  }

  for (std::size_t index = 0; index < sizeof(kRegisteredSignals) /
                                          sizeof(kRegisteredSignals[0]);
       ++index) {
    const std::uint8_t bit = static_cast<std::uint8_t>(1U << index);
    if (!registry_.register_handler(
            kRegisteredSignals[index],
            &EspIdfBootClockEventBridge::on_registered_event,
            this)) {
      const EspIdfBootClockRegistrationResult cleanup_result =
          unregister_handlers();
      if (cleanup_result == EspIdfBootClockRegistrationResult::CleanupFailed) {
        return EspIdfBootClockRegistrationResult::CleanupFailed;
      }
      return EspIdfBootClockRegistrationResult::RegistrationFailed;
    }
    registered_handler_mask_ = static_cast<std::uint8_t>(
        registered_handler_mask_ | bit);
  }

  return EspIdfBootClockRegistrationResult::Registered;
}

EspIdfBootClockRegistrationResult
EspIdfBootClockEventBridge::unregister_handlers() {
  if (registered_handler_mask_ == 0) {
    return EspIdfBootClockRegistrationResult::NotRegistered;
  }

  bool cleanup_failed = false;
  for (std::size_t index = sizeof(kRegisteredSignals) /
                              sizeof(kRegisteredSignals[0]);
       index > 0;
       --index) {
    const std::size_t signal_index = index - 1;
    const std::uint8_t bit = static_cast<std::uint8_t>(1U << signal_index);
    if ((registered_handler_mask_ & bit) == 0) {
      continue;
    }

    if (registry_.unregister_handler(
            kRegisteredSignals[signal_index],
            &EspIdfBootClockEventBridge::on_registered_event,
            this)) {
      registered_handler_mask_ = static_cast<std::uint8_t>(
          registered_handler_mask_ & static_cast<std::uint8_t>(~bit));
    } else {
      cleanup_failed = true;
    }
  }

  return cleanup_failed ? EspIdfBootClockRegistrationResult::CleanupFailed
                        : EspIdfBootClockRegistrationResult::Unregistered;
}

void EspIdfBootClockEventBridge::report_event_loop_creation(bool succeeded) {
  handle(succeeded ? EspIdfBootClockSignal::EventLoopCreationSucceeded
                   : EspIdfBootClockSignal::EventLoopCreationFailed);
}

void EspIdfBootClockEventBridge::handle(EspIdfBootClockSignal signal) {
  switch (signal) {
    case EspIdfBootClockSignal::EventLoopCreationSucceeded:
      prerequisites_.mark_event_loop_ready();
      return;
    case EspIdfBootClockSignal::StationGotIp:
      prerequisites_.mark_network_ready();
      return;
    case EspIdfBootClockSignal::StationLostIp:
    case EspIdfBootClockSignal::StationDisconnected:
      prerequisites_.mark_network_unready();
      return;
    case EspIdfBootClockSignal::EventLoopCreationFailed:
    case EspIdfBootClockSignal::StationConnected:
    case EspIdfBootClockSignal::Unrelated:
      return;
  }
}

void EspIdfBootClockEventBridge::on_registered_event(
    void* context,
    EspIdfBootClockSignal signal) {
  auto* bridge = static_cast<EspIdfBootClockEventBridge*>(context);
  if (bridge != nullptr) {
    bridge->handle(signal);
  }
}

}  // namespace frost
