#include "ecy_stat_ble_transport.hpp"
#include "esp32_session.hpp"
#include "ecy_stat_codec.hpp"
#include "esp_log.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <cstdio>
#include <cstdlib>
namespace {
constexpr char tag[] = "frost_setup";
frost::PeerCandidate candidates[16]{};
unsigned read_choice() {
  char line[12]{}; unsigned n = 0; bool overflow = false;
  for (;;) {
    int c = fgetc(stdin);
    if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    if (c == '\r' || c == '\n') {
      if (n == 0 && !overflow) continue;
      if (overflow) return 0;
      char* end = nullptr;
      const unsigned long value = std::strtoul(line, &end, 10);
      return end != line && *end == '\0' && value <= 16 ? static_cast<unsigned>(value) : 0;
    }
    if (c < '0' || c > '9' || n == sizeof(line) - 1) overflow = true;
    else line[n++] = static_cast<char>(c);
  }
}
void run(void*) {
  if (nvs_flash_init() != ESP_OK) {
    ESP_LOGE(tag, "NVS initialization failed; stored data preserved"); vTaskDelete(nullptr); return;
  }
  ble_addr_t existing{};
  if (frost::load_selected_peer(existing)) {
    ESP_LOGI(tag, "A thermostat is already selected. No pairing or replacement attempted.");
    vTaskDelete(nullptr); return;
  }
  if (!frost::start_ble_session()) { vTaskDelete(nullptr); return; }
  for (unsigned i = 0; i < 100 && !frost::ble_session_ready(); ++i) vTaskDelay(pdMS_TO_TICKS(100));
  std::size_t count = 0;
  if (!frost::scan_thermostats(candidates, 16, count) || count == 0) {
    ESP_LOGE(tag, "No candidates; reboot to retry"); vTaskDelete(nullptr); return;
  }
  puts("Nearby ECY-STAT candidates (identifiers are private; do not publish this output):");
  for (std::size_t i = 0; i < count; ++i) {
    const auto& a = candidates[i].address;
    printf("%u: %02x:%02x:%02x:%02x:%02x:%02x type=%u RSSI=%d\n", unsigned(i + 1),
        a.val[5], a.val[4], a.val[3], a.val[2], a.val[1], a.val[0], a.type, candidates[i].rssi);
  }
  puts("Choose YOUR thermostat's number. Signal strength alone does not prove ownership.");
  const unsigned choice = read_choice();
  if (choice == 0 || choice > count) { puts("Invalid selection; reboot to retry."); vTaskDelete(nullptr); return; }
  static frost::EcyStatBleTransport transport(candidates[choice - 1].address, true);
  std::uint8_t target[64]{}, fan[64]{}; std::size_t nt = 0, nf = 0;
  frost::ThermostatReadback state{}; ble_addr_t identity{};
  const bool verified = transport.connect() && transport.discover() &&
      transport.read(frost::kTargetStateCharacteristicUuid, target, sizeof(target), nt) &&
      transport.read(frost::kFanStateCharacteristicUuid, fan, sizeof(fan), nf) &&
      frost::decode_readback(target, nt, fan, nf, state) == frost::CodecError::Ok &&
      transport.paired_identity(identity);
  if (verified && frost::save_selected_peer(identity)) {
    ESP_LOGI(tag, "Authenticated readback: target=%.1fF fan=%s. Selected identity saved.",
        state.target_temperature_f, state.fan_mode == frost::FanMode::On ? "On" : "Auto");
    puts("Check against your thermostat. No thermostat settings were written.");
  } else {
    ESP_LOGE(tag, "Setup incomplete. No selected identity saved. Bonds preserved; reboot to retry.");
  }
  transport.disconnect();
  vTaskDelete(nullptr);
}
}
extern "C" void app_main() {
  if (xTaskCreate(run, "frost_setup", 8192, nullptr, 5, nullptr) != pdPASS)
    ESP_LOGE(tag, "Cannot start setup task");
}
