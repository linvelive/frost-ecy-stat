#pragma once
#include "host/ble_gap.h"
#include <cstddef>
#include <cstdint>
namespace frost {
// Call once, after nvs_flash_init(), from an application task, not a BLE callback.
bool start_ble_session();
bool ble_session_ready();
struct PeerCandidate { ble_addr_t address; std::int8_t rssi; };
// Blocks for a bounded scan. No connection is made. Addresses are local/private.
bool scan_thermostats(PeerCandidate* candidates, std::size_t capacity, std::size_t& count);
bool load_selected_peer(ble_addr_t& peer);
// Called only after authenticated readback during explicit commissioning.
bool save_selected_peer(const ble_addr_t& peer);
}  // namespace frost
