#include "production_boot.hpp"

#include "boot_clock.hpp"
#include "controller_schedule_applier.hpp"
#include "ecy_stat_ble_transport.hpp"
#include "esp32_session.hpp"
#include "esp_boot_clock_event_bridge.hpp"
#include "esp_sntp_backend.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

#include <atomic>
#include <cstring>

#ifdef FROST_RUNTIME_CONFIG_HEADER
#include FROST_RUNTIME_CONFIG_HEADER
#else
// Unconfigured builds compile the complete path but cannot activate it.
#define FROST_WIFI_SSID ""
#define FROST_WIFI_PASSWORD ""
#define FROST_POSIX_TIMEZONE ""
#define FROST_SNTP_SERVER ""
inline constexpr frost::ScheduleEntry kSchedule[] = {{8, 0, {68.0f, frost::FanMode::Auto}}};
#endif


namespace frost {
namespace {
constexpr char kTag[] = "frost_boot";
constexpr std::uint32_t kSyncTimeoutMs = 30000;
constexpr std::uint32_t kReconnectIntervalMs = 10000;
constexpr std::uint32_t kScheduleIntervalMs = 10000;

class EspMonotonicClock final : public MonotonicClock {
 public:
  std::uint32_t now_ms() const override {
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
  }
};

const char* clock_status(BootClockTickResult result) {
  switch (result) {
    case BootClockTickResult::WaitingForPrerequisites: return "waiting-network";
    case BootClockTickResult::WaitingForSynchronization: return "sntp-started-waiting-sync";
    case BootClockTickResult::Ready: return "synchronized";
    case BootClockTickResult::SynchronizationTimedOut: return "sync-timeout-clock-unavailable";
    case BootClockTickResult::InitializationFailed: return "sntp-init-failed";
  }
  return "unknown";
}

bool start_wifi() {
  if (esp_netif_create_default_wifi_sta() == nullptr) {
    return false;
  }
  const wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) {
    return false;
  }
  wifi_config_t config{};
  static_assert(sizeof(FROST_WIFI_SSID) - 1 <= sizeof(config.sta.ssid));
  static_assert(sizeof(FROST_WIFI_PASSWORD) - 1 <= sizeof(config.sta.password));
  std::memcpy(config.sta.ssid, FROST_WIFI_SSID, sizeof(FROST_WIFI_SSID) - 1);
  std::memcpy(config.sta.password, FROST_WIFI_PASSWORD, sizeof(FROST_WIFI_PASSWORD) - 1);
  config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  config.sta.pmf_cfg.capable = true;
  if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
      esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK ||
      esp_wifi_start() != ESP_OK) {
    (void)esp_wifi_stop();
    (void)esp_wifi_deinit();
    return false;
  }
  return true;
}
}  // namespace

void run_configured_boot() {
  if (FROST_WIFI_SSID[0] == '\0' || FROST_WIFI_PASSWORD[0] == '\0' ||
      FROST_POSIX_TIMEZONE[0] == '\0' || FROST_SNTP_SERVER[0] == '\0') {
    ESP_LOGW(kTag, "configuration absent; radios and scheduler remain stopped");
    return;
  }

  // Preserve persisted BLE bonds, even if NVS initialization fails.
  if (nvs_flash_init() != ESP_OK || esp_netif_init() != ESP_OK) {
    ESP_LOGE(kTag, "NVS/netif initialization failed; stopped");
    return;
  }
  if (esp_event_loop_create_default() != ESP_OK) {
    ESP_LOGE(kTag, "event-loop initialization failed; stopped");
    return;
  }

  // Static lifetimes keep callback contexts alive, including failure exits.
  static EspIdfBootClockPrerequisites prerequisites;
  static EspIdfBootClockEventRegistryAdapter registry;
  static EspIdfBootClockEventBridge bridge(prerequisites, registry);
  if (bridge.register_handlers() != EspIdfBootClockRegistrationResult::Registered) {
    ESP_LOGE(kTag, "readiness registration failed; stopped");
    return;
  }
  bridge.report_event_loop_creation(true);
  ESP_LOGI(kTag, "event-loop ready; network unready");
  // Suppress ESP-IDF's default network-identifier messages.
  esp_log_level_set("wifi", ESP_LOG_ERROR);
  esp_log_level_set("esp_netif_handlers", ESP_LOG_ERROR);
  if (!start_wifi()) {
    ESP_LOGE(kTag, "station initialization failed; stopped");
    (void)bridge.unregister_handlers();
    return;
  }

  static EspIdfSntpBackend backend;
  static SntpNetworkTimeSource source(backend);
  static EspMonotonicClock monotonic;
  static BootClockCoordinator coordinator(
      source, prerequisites, monotonic, FROST_SNTP_SERVER, kSyncTimeoutMs);
  static NetworkSynchronizedClock clock(source, FROST_POSIX_TIMEZONE);
  // Production reuses existing bonds; it cannot prompt for a PIN or replace one.
  ble_addr_t peer{};
  if (!load_selected_peer(peer)) {
    ESP_LOGE(kTag, "No selected thermostat; run commissioning first");
    return;
  }
  static EcyStatBleTransport transport(peer);
  static EcyStatController controller(transport);
  static ControllerScheduleApplier applier(controller);
  static ScheduleRunner runner(clock, applier, kSchedule, sizeof(kSchedule) / sizeof(kSchedule[0]));
  if (!runner.valid()) {
    ESP_LOGE(kTag, "Invalid schedule: use sorted, unique daily times and valid states");
    return;
  }

  bool first_tick = true;
  bool previous_network_ready = false;
  bool ble_started = false;
  bool clock_reported_ready = false;
  auto previous_clock_status = BootClockTickResult::WaitingForPrerequisites;
  std::uint32_t last_connect_ms = monotonic.now_ms() - kReconnectIntervalMs;
  std::uint32_t last_schedule_ms = monotonic.now_ms() - kScheduleIntervalMs;
  for (;;) {
    const auto now = monotonic.now_ms();
    const bool network_ready = prerequisites.ready();
    if (first_tick || network_ready != previous_network_ready) {
      ESP_LOGI(kTag, "network readiness=%s", network_ready ? "ready" : "unready");
      previous_network_ready = network_ready;
    }
    if (!network_ready && now - last_connect_ms >= kReconnectIntervalMs) {
      last_connect_ms = now;
      const auto result = esp_wifi_connect();
      ESP_LOGI(kTag, "station connect request result=%d", result);
    }
    const auto status = coordinator.tick();
    if (first_tick || status != previous_clock_status) {
      ESP_LOGI(kTag, "boot clock=%s", clock_status(status));
      previous_clock_status = status;
    }
    first_tick = false;
    LocalTime local_time{};
    if (status == BootClockTickResult::Ready &&
        clock.read(local_time) == LocalClock::ReadResult::Ready) {
      if (!clock_reported_ready) {
        ESP_LOGI(kTag, "trusted local clock ready at %02u:%02u", local_time.hour, local_time.minute);
        clock_reported_ready = true;
      }
      {
        if (!ble_started) {
          if (!start_ble_session()) {
            ESP_LOGE(kTag, "BLE initialization failed; scheduler stopped");
            return;
          }
          ble_started = true;
        }
        if (ble_session_ready() &&
            now - last_schedule_ms >= kScheduleIntervalMs) {
          last_schedule_ms = now;
          const auto result = runner.tick();
          if (result != ScheduleTickResult::NoOp)
            ESP_LOGI(kTag, "schedule result=%u", static_cast<unsigned>(result));
          if (result == ScheduleTickResult::Applied || result == ScheduleTickResult::ApplyFailed) {
            const auto& applied = applier.last_result();
            ESP_LOGI(kTag, "verification=%u target_status=%u target=%.1fF fan_status=%u fan=%u",
                     static_cast<unsigned>(applied.status),
                     static_cast<unsigned>(applied.target.status), applied.target.readback_temperature_f,
                     static_cast<unsigned>(applied.fan.status), static_cast<unsigned>(applied.fan.readback_mode));
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
void run_production_boot() {
  // Controller verification can block on BLE; give it a dedicated stack and
  // keep it outside both ESP event callbacks and the NimBLE host task.
  const auto result = xTaskCreate([](void*) {
    run_configured_boot();
    vTaskDelete(nullptr);
  }, "frost_runtime", 8192, nullptr, 5, nullptr);
  if (result != pdPASS) {
    ESP_LOGE(kTag, "runtime task creation failed; stopped");
  }
}
}  // namespace frost
