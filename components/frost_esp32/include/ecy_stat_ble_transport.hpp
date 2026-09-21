#pragma once

#include "ecy_stat_controller.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "host/ble_gap.h"

#include <cstddef>
#include <cstdint>

namespace frost {

class EcyStatBleTransport final : public BleTransport {
 public:
  // One long-lived instance, called from one task; NimBLE must already be ready.
  explicit EcyStatBleTransport(const ble_addr_t& peer, bool allow_pairing_input = false);
  bool paired_identity(ble_addr_t& identity) const;
  ~EcyStatBleTransport() override;

  bool connect() override;
  bool discover() override;
  bool write_with_response(
      const char* characteristic_uuid,
      const std::uint8_t* payload,
      std::size_t length) override;
  bool read(
      const char* characteristic_uuid,
      std::uint8_t* output,
      std::size_t capacity,
      std::size_t& length) override;
  void delay_ms(std::uint32_t milliseconds) override;
  void disconnect() override;

 private:
  enum class PendingOperation : std::uint8_t {
    None,
    Connect,
    Disconnect,
    Discover,
    Read,
    Write,
  };

  enum class CharacteristicQuery : std::uint8_t {
    Command,
    Target,
    Fan,
  };

  static int gap_event(struct ble_gap_event* event, void* arg);
  static int service_discovery_callback(
      std::uint16_t conn_handle,
      const struct ble_gatt_error* error,
      const struct ble_gatt_svc* service,
      void* arg);
  static int characteristic_discovery_callback(
      std::uint16_t conn_handle,
      const struct ble_gatt_error* error,
      const struct ble_gatt_chr* characteristic,
      void* arg);
  static int read_callback(
      std::uint16_t conn_handle,
      const struct ble_gatt_error* error,
      struct ble_gatt_attr* attribute,
      void* arg);
  static int write_callback(
      std::uint16_t conn_handle,
      const struct ble_gatt_error* error,
      struct ble_gatt_attr* attribute,
      void* arg);

  bool begin_operation(PendingOperation operation);
  bool wait_for_operation(TickType_t timeout);
  void complete_operation(bool success);
  bool begin_security_if_required(std::uint16_t status);
  bool retry_pending_io();
  int start_characteristic_discovery(CharacteristicQuery query);
  int start_pending_read();
  int start_pending_write();
  void fail_pending_operation();
  void reset_connection_state();

  const bool allow_pairing_input_;
  const ble_addr_t peer_;
  SemaphoreHandle_t completion_ = nullptr;
  PendingOperation pending_operation_ = PendingOperation::None;
  CharacteristicQuery characteristic_query_ = CharacteristicQuery::Command;

  bool operation_success_ = false;
  bool connected_ = false;
  bool discovered_ = false;
  bool security_attempted_ = false;
  std::uint8_t own_address_type_ = 0;
  std::uint16_t conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
  std::uint16_t command_value_handle_ = 0;
  std::uint16_t target_value_handle_ = 0;
  std::uint16_t fan_value_handle_ = 0;

  const std::uint8_t* pending_write_payload_ = nullptr;
  std::size_t pending_write_length_ = 0;
  std::uint16_t pending_read_handle_ = 0;
  std::uint8_t* pending_read_output_ = nullptr;
  std::size_t pending_read_capacity_ = 0;
  std::size_t* pending_read_length_ = nullptr;
};

}  // namespace frost
