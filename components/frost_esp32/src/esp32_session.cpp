#include "esp32_session.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include <atomic>
#include <cstring>
extern "C" void ble_store_config_init(void);
namespace frost {
namespace {
std::atomic<bool> ready{false};
void sync() { ready.store(true); }
void reset(int) { ready.store(false); }
void host(void*) { nimble_port_run(); ready.store(false); nimble_port_freertos_deinit(); }
// Static callback context survives scan cancellation. One application task owns scans.
struct Scan {
  PeerCandidate* items = nullptr;
  std::size_t capacity = 0, count = 0;
  SemaphoreHandle_t done = nullptr;
} scan;
int on_scan(ble_gap_event* event, void*) {
  if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
    xSemaphoreGive(scan.done);
  } else if (event->type == BLE_GAP_EVENT_DISC && scan.items != nullptr) {
    ble_hs_adv_fields fields{};
    if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0 ||
        fields.name == nullptr || fields.name_len < 8 || std::memcmp(fields.name, "ECY-STAT", 8) != 0) return 0;
    for (std::size_t i = 0; i < scan.count; ++i) {
      if (ble_addr_cmp(&scan.items[i].address, &event->disc.addr) == 0) return 0;
    }
    if (scan.count < scan.capacity) scan.items[scan.count++] = {event->disc.addr, event->disc.rssi};
  }
  return 0;
}
}
bool start_ble_session() {
  if (nimble_port_init() != ESP_OK) return false;
  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_KEYBOARD_ONLY;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.reset_cb = reset;
  ble_hs_cfg.sync_cb = sync;
  // Do not silently evict another stored bond to make room.
  ble_store_config_init();
  nimble_port_freertos_init(host);
  return true;
}
bool ble_session_ready() { return ready.load(); }
bool scan_thermostats(PeerCandidate* candidates, std::size_t capacity, std::size_t& count) {
  count = 0;
  if (!ready.load() || candidates == nullptr || capacity == 0) return false;
  if (scan.done == nullptr) scan.done = xSemaphoreCreateBinary();
  if (scan.done == nullptr) return false;
  while (xSemaphoreTake(scan.done, 0) == pdTRUE) {}
  scan.items = candidates; scan.capacity = capacity; scan.count = 0;
  std::uint8_t address_type = 0;
  if (ble_hs_id_infer_auto(0, &address_type) != 0) return false;
  ble_gap_disc_params params{};
  params.filter_duplicates = 1;
  if (ble_gap_disc(address_type, 10000, &params, on_scan, nullptr) != 0) return false;
  if (xSemaphoreTake(scan.done, pdMS_TO_TICKS(12000)) != pdTRUE) {
    ble_gap_disc_cancel();
    return false;
  }
  count = scan.count;
  return true;
}
bool load_selected_peer(ble_addr_t& peer) {
  nvs_handle_t handle;
  if (nvs_open("frost_peer", NVS_READONLY, &handle) != ESP_OK) return false;
  std::uint8_t data[7]{}; std::size_t length = sizeof(data);
  const auto result = nvs_get_blob(handle, "identity", data, &length);
  nvs_close(handle);
  if (result != ESP_OK || length != sizeof(data) || data[0] > BLE_ADDR_RANDOM_ID) return false;
  peer.type = data[0]; std::memcpy(peer.val, data + 1, 6);
  return true;
}
bool save_selected_peer(const ble_addr_t& peer) {
  nvs_handle_t handle;
  if (nvs_open("frost_peer", NVS_READWRITE, &handle) != ESP_OK) return false;
  std::uint8_t data[7]{}; data[0] = peer.type; std::memcpy(data + 1, peer.val, 6);
  auto result = nvs_set_blob(handle, "identity", data, sizeof(data));
  if (result == ESP_OK) result = nvs_commit(handle);
  nvs_close(handle);
  return result == ESP_OK;
}
}  // namespace frost
