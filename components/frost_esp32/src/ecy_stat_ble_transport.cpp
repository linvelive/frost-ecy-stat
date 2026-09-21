#include "ecy_stat_ble_transport.hpp"

#include "ecy_stat_codec.hpp"

#include "esp_log.h"
#include <cstdio>
#include <atomic>

#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"

#include "freertos/task.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>
#include <cmath>

namespace frost {
namespace {

constexpr char kTag[] = "ecy_stat_transport";
constexpr int32_t kConnectDurationMs = 30000;
constexpr TickType_t kConnectWait = pdMS_TO_TICKS(45000);
constexpr TickType_t kOperationWait = pdMS_TO_TICKS(30000);
constexpr uint16_t kGattFirstHandle = 1;
constexpr uint16_t kGattLastHandle = 0xffff;
// NimBLE stores 128-bit UUID bytes least-significant byte first.
const ble_uuid128_t kCommandUuid = BLE_UUID128_INIT(
    0xef, 0x8e, 0x5a, 0x7e, 0x13, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00);
const ble_uuid128_t kTargetUuid = BLE_UUID128_INIT(
    0xef, 0x8e, 0x5a, 0x7e, 0x13, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00);
const ble_uuid128_t kFanUuid = BLE_UUID128_INIT(
    0xef, 0x8e, 0x5a, 0x7e, 0x13, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x00);

std::atomic<uint16_t> g_pending_passkey_conn_handle{BLE_HS_CONN_HANDLE_NONE};

bool is_security_required_error(uint16_t status) {
  return status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHEN) ||
         status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_ENC) ||
         status == BLE_HS_ATT_ERR(BLE_ATT_ERR_INSUFFICIENT_AUTHOR);
}

void passkey_input_task(void*) {
  std::uint16_t input_connection = BLE_HS_CONN_HANDLE_NONE;
  unsigned digits = 0;
  std::uint32_t value = 0;
  bool invalid = false;
  for (;;) {
    const auto pending = g_pending_passkey_conn_handle.load();
    if (pending != input_connection) {
      input_connection = pending; digits = 0; value = 0; invalid = false;
    }
    const int c = fgetc(stdin);
    if (c == EOF) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    if (pending == BLE_HS_CONN_HANDLE_NONE) continue;
    if (c != '\r' && c != '\n') {
      if (c < '0' || c > '9' || digits == 6) invalid = true;
      else { value = value * 10 + static_cast<unsigned>(c - '0'); ++digits; }
      continue;
    }
    if (digits != 6 || invalid) {
      ESP_LOGW(kTag, "Enter exactly six PIN digits, then Enter");
      digits = 0; value = 0; invalid = false;
      continue;
    }
    auto expected = pending;
    if (g_pending_passkey_conn_handle.compare_exchange_strong(expected, BLE_HS_CONN_HANDLE_NONE)) {
      ble_sm_io io{}; io.action = BLE_SM_IOACT_INPUT; io.passkey = value;
      const int result = ble_sm_inject_io(pending, &io);
      io.passkey = 0;
      ESP_LOGI(kTag, "PIN submitted: result=%d", result);
    }
    digits = 0; value = 0;
  }
}

}  // namespace

EcyStatBleTransport::EcyStatBleTransport(const ble_addr_t& peer, bool allow_pairing_input)
    : allow_pairing_input_(allow_pairing_input), peer_(peer), completion_(xSemaphoreCreateBinary()) {}

EcyStatBleTransport::~EcyStatBleTransport() {
  disconnect();
  if (completion_ != nullptr) {
    vSemaphoreDelete(completion_);
    completion_ = nullptr;
  }
}

bool EcyStatBleTransport::begin_operation(PendingOperation operation) {
  if (completion_ == nullptr || pending_operation_ != PendingOperation::None) {
    return false;
  }
  while (xSemaphoreTake(completion_, 0) == pdTRUE) {
  }
  operation_success_ = false;
  pending_operation_ = operation;
  return true;
}

bool EcyStatBleTransport::wait_for_operation(TickType_t timeout) {
  if (completion_ == nullptr ||
      xSemaphoreTake(completion_, timeout) != pdTRUE) {
    pending_operation_ = PendingOperation::None;
    return false;
  }
  return operation_success_;
}

void EcyStatBleTransport::complete_operation(bool success) {
  if (pending_operation_ == PendingOperation::None) {
    return;
  }
  operation_success_ = success;
  pending_operation_ = PendingOperation::None;
  xSemaphoreGive(completion_);
}

void EcyStatBleTransport::fail_pending_operation() {
  complete_operation(false);
}

void EcyStatBleTransport::reset_connection_state() {
  connected_ = false;
  discovered_ = false;
  conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
  command_value_handle_ = 0;
  target_value_handle_ = 0;
  fan_value_handle_ = 0;
  pending_operation_ = PendingOperation::None;
  pending_write_payload_ = nullptr;
  pending_write_length_ = 0;
  pending_read_handle_ = 0;
  pending_read_output_ = nullptr;
  pending_read_capacity_ = 0;
  pending_read_length_ = nullptr;
}

bool EcyStatBleTransport::connect() {
  // Never reuse a session whose asynchronous termination has not completed.
  if (!connected_ && conn_handle_ != BLE_HS_CONN_HANDLE_NONE) {
    disconnect();
    if (conn_handle_ != BLE_HS_CONN_HANDLE_NONE) {
      return false;
    }
  }
  if (connected_) {
    return true;
  }

  reset_connection_state();

  static bool passkey_task_started = false;
  if (allow_pairing_input_ && !passkey_task_started) {
    const BaseType_t task_result = xTaskCreate(
        passkey_input_task,
        "frost_pin",
        3072,
        nullptr,
        4,
        nullptr);
    if (task_result != pdPASS) {
      ESP_LOGE(kTag, "cannot start PIN input task");
      return false;
    }
    passkey_task_started = true;
  }

  if (ble_hs_id_infer_auto(0, &own_address_type_) != 0) {
    ESP_LOGE(kTag, "cannot determine local BLE address type");
    return false;
  }

  if (!begin_operation(PendingOperation::Connect)) {
    return false;
  }

  const int result = ble_gap_connect(own_address_type_, &peer_,
      kConnectDurationMs, nullptr, gap_event, this);
  if (result != 0) {
    pending_operation_ = PendingOperation::None;
    return false;
  }
  if (!wait_for_operation(kConnectWait)) {
    ble_gap_conn_cancel();
    return false;
  }
  return connected_;
}

bool EcyStatBleTransport::discover() {
  if (!connected_) {
    return false;
  }
  if (discovered_) {
    return true;
  }

  command_value_handle_ = 0;
  target_value_handle_ = 0;
  fan_value_handle_ = 0;
  if (!begin_operation(PendingOperation::Discover)) {
    return false;
  }

  const int result = ble_gattc_disc_all_svcs(
      conn_handle_, service_discovery_callback, this);
  if (result != 0) {
    pending_operation_ = PendingOperation::None;
    ESP_LOGE(kTag, "cannot start service discovery: rc=%d", result);
    return false;
  }
  return wait_for_operation(kOperationWait);
}

bool EcyStatBleTransport::write_with_response(
    const char* characteristic_uuid,
    const std::uint8_t* payload,
    std::size_t length) {
  if (!discovered_ || characteristic_uuid == nullptr || payload == nullptr ||
      length == 0 ||
      std::strcmp(characteristic_uuid, kCommandCharacteristicUuid) != 0 ||
      command_value_handle_ == 0) {
    return false;
  }

  ESP_LOGI("frost_boot", "BLE write requested bytes=%u", static_cast<unsigned>(length));
  pending_write_payload_ = payload;
  pending_write_length_ = length;
  pending_read_output_ = nullptr;
  pending_read_length_ = nullptr;
  pending_read_handle_ = 0;
  security_attempted_ = false;
  if (!begin_operation(PendingOperation::Write)) {
    return false;
  }

  if (start_pending_write() != 0) {
    pending_operation_ = PendingOperation::None;
    return false;
  }
  const bool completed = wait_for_operation(kOperationWait);
  pending_write_payload_ = nullptr;
  pending_write_length_ = 0;
  return completed;
}

bool EcyStatBleTransport::read(
    const char* characteristic_uuid,
    std::uint8_t* output,
    std::size_t capacity,
    std::size_t& length) {
  length = 0;
  if (!discovered_ || characteristic_uuid == nullptr || output == nullptr ||
      capacity == 0) {
    return false;
  }

  std::uint16_t* selected_handle = nullptr;
  if (std::strcmp(characteristic_uuid, kTargetStateCharacteristicUuid) == 0) {
    selected_handle = &target_value_handle_;
  } else if (
      std::strcmp(characteristic_uuid, kFanStateCharacteristicUuid) == 0) {
    selected_handle = &fan_value_handle_;
  } else {
    return false;
  }
  if (*selected_handle == 0) {
    return false;
  }

  pending_read_output_ = output;
  pending_read_capacity_ = capacity;
  pending_read_length_ = &length;
  pending_read_handle_ = *selected_handle;
  pending_write_payload_ = nullptr;
  security_attempted_ = false;
  if (!begin_operation(PendingOperation::Read)) {
    return false;
  }

  if (start_pending_read() != 0) {
    pending_operation_ = PendingOperation::None;
    return false;
  }
  const bool completed = wait_for_operation(kOperationWait);
  pending_read_output_ = nullptr;
  pending_read_capacity_ = 0;
  pending_read_length_ = nullptr;
  pending_read_handle_ = 0;
  return completed;
}

void EcyStatBleTransport::delay_ms(std::uint32_t milliseconds) {
  vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

void EcyStatBleTransport::disconnect() {
  if (conn_handle_ != BLE_HS_CONN_HANDLE_NONE) {
    connected_ = false;
    if (!begin_operation(PendingOperation::Disconnect)) {
      return;
    }
    const int result = ble_gap_terminate(conn_handle_, BLE_ERR_REM_USER_CONN_TERM);
    if (result == 0) {
      if (!wait_for_operation(kOperationWait)) {
        ESP_LOGW(kTag, "disconnect completion unavailable; reconnect blocked");
        return;
      }
    } else {
      pending_operation_ = PendingOperation::None;
      if (result != BLE_HS_ENOTCONN) {
        ESP_LOGW(kTag, "could not terminate BLE connection: rc=%d", result);
        return;
      }
    }
  }
  reset_connection_state();
}

bool EcyStatBleTransport::begin_security_if_required(std::uint16_t status) {
  if (!is_security_required_error(status) || security_attempted_) {
    return false;
  }

  security_attempted_ = true;
  const int result = ble_gap_security_initiate(conn_handle_);
  if (result != 0 && result != BLE_HS_EALREADY) {
    ESP_LOGW(kTag, "cannot start BLE security: rc=%d", result);
    complete_operation(false);
    return false;
  }
  ESP_LOGI(kTag, "BLE security requested; waiting for encryption");
  return true;
}

bool EcyStatBleTransport::retry_pending_io() {
  const int result = pending_operation_ == PendingOperation::Read
                         ? start_pending_read()
                         : start_pending_write();
  if (result != 0) {
    complete_operation(false);
    return false;
  }
  return true;
}

int EcyStatBleTransport::start_pending_read() {
  if (pending_operation_ != PendingOperation::Read ||
      pending_read_handle_ == 0 || pending_read_output_ == nullptr ||
      pending_read_length_ == nullptr) {
    return BLE_HS_EINVAL;
  }
  return ble_gattc_read(
      conn_handle_, pending_read_handle_, read_callback, this);
}

int EcyStatBleTransport::start_pending_write() {
  if (pending_operation_ != PendingOperation::Write ||
      pending_write_payload_ == nullptr || pending_write_length_ == 0 ||
      command_value_handle_ == 0) {
    return BLE_HS_EINVAL;
  }
  return ble_gattc_write_flat(
      conn_handle_,
      command_value_handle_,
      pending_write_payload_,
      static_cast<std::uint16_t>(pending_write_length_),
      write_callback,
      this);
}

int EcyStatBleTransport::start_characteristic_discovery(
    CharacteristicQuery query) {
  characteristic_query_ = query;
  const ble_uuid_t* uuid = nullptr;
  switch (query) {
    case CharacteristicQuery::Command:
      uuid = &kCommandUuid.u;
      break;
    case CharacteristicQuery::Target:
      uuid = &kTargetUuid.u;
      break;
    case CharacteristicQuery::Fan:
      uuid = &kFanUuid.u;
      break;
  }
  return ble_gattc_disc_chrs_by_uuid(
      conn_handle_,
      kGattFirstHandle,
      kGattLastHandle,
      uuid,
      characteristic_discovery_callback,
      this);
}

int EcyStatBleTransport::service_discovery_callback(
    std::uint16_t conn_handle,
    const struct ble_gatt_error* error,
    const struct ble_gatt_svc* service,
    void* arg) {
  auto* transport = static_cast<EcyStatBleTransport*>(arg);
  if (transport == nullptr || transport->conn_handle_ != conn_handle ||
      transport->pending_operation_ != PendingOperation::Discover) {
    return 0;
  }

  if (error->status == 0 && service != nullptr) {
    return 0;
  }
  if (error->status == BLE_HS_EDONE) {
    const int result = transport->start_characteristic_discovery(
        CharacteristicQuery::Command);
    if (result != 0) {
      transport->complete_operation(false);
    }
    return 0;
  }

  transport->complete_operation(false);
  return 0;
}

int EcyStatBleTransport::characteristic_discovery_callback(
    std::uint16_t conn_handle,
    const struct ble_gatt_error* error,
    const struct ble_gatt_chr* characteristic,
    void* arg) {
  auto* transport = static_cast<EcyStatBleTransport*>(arg);
  if (transport == nullptr || transport->conn_handle_ != conn_handle ||
      transport->pending_operation_ != PendingOperation::Discover) {
    return 0;
  }

  if (error->status == 0 && characteristic != nullptr) {
    switch (transport->characteristic_query_) {
      case CharacteristicQuery::Command:
        transport->command_value_handle_ = characteristic->val_handle;
        break;
      case CharacteristicQuery::Target:
        transport->target_value_handle_ = characteristic->val_handle;
        break;
      case CharacteristicQuery::Fan:
        transport->fan_value_handle_ = characteristic->val_handle;
        break;
    }
    return 0;
  }

  if (error->status != BLE_HS_EDONE) {
    transport->complete_operation(false);
    return 0;
  }

  CharacteristicQuery next_query = CharacteristicQuery::Fan;
  bool finished = false;
  switch (transport->characteristic_query_) {
    case CharacteristicQuery::Command:
      if (transport->command_value_handle_ == 0) {
        transport->complete_operation(false);
        return 0;
      }
      next_query = CharacteristicQuery::Target;
      break;
    case CharacteristicQuery::Target:
      if (transport->target_value_handle_ == 0) {
        transport->complete_operation(false);
        return 0;
      }
      next_query = CharacteristicQuery::Fan;
      break;
    case CharacteristicQuery::Fan:
      if (transport->fan_value_handle_ == 0) {
        transport->complete_operation(false);
        return 0;
      }
      finished = true;
      break;
  }

  if (finished) {
    transport->discovered_ = true;
    transport->complete_operation(true);
    return 0;
  }

  const int result = transport->start_characteristic_discovery(next_query);
  if (result != 0) {
    transport->complete_operation(false);
  }
  return 0;
}

int EcyStatBleTransport::read_callback(
    std::uint16_t conn_handle,
    const struct ble_gatt_error* error,
    struct ble_gatt_attr* attribute,
    void* arg) {
  auto* transport = static_cast<EcyStatBleTransport*>(arg);
  if (transport == nullptr || transport->conn_handle_ != conn_handle ||
      transport->pending_operation_ != PendingOperation::Read) {
    return 0;
  }

  if (error->status == 0 && attribute != nullptr &&
      transport->pending_read_output_ != nullptr &&
      transport->pending_read_length_ != nullptr) {
    std::uint16_t copied_length = 0;
    const int result = ble_hs_mbuf_to_flat(
        attribute->om,
        transport->pending_read_output_,
        static_cast<std::uint16_t>(transport->pending_read_capacity_),
        &copied_length);
    if (result == 0) {
      *transport->pending_read_length_ = copied_length;
      transport->complete_operation(true);
    } else {
      transport->complete_operation(false);
    }
    return 0;
  }

  if (transport->begin_security_if_required(error->status)) {
    return 0;
  }
  transport->complete_operation(false);
  return 0;
}

int EcyStatBleTransport::write_callback(
    std::uint16_t conn_handle,
    const struct ble_gatt_error* error,
    struct ble_gatt_attr*,
    void* arg) {
  auto* transport = static_cast<EcyStatBleTransport*>(arg);
  if (transport == nullptr || transport->conn_handle_ != conn_handle ||
      transport->pending_operation_ != PendingOperation::Write) {
    return 0;
  }

  if (error->status == 0) {
    transport->complete_operation(true);
    return 0;
  }
  if (transport->begin_security_if_required(error->status)) {
    return 0;
  }
  transport->complete_operation(false);
  return 0;
}

int EcyStatBleTransport::gap_event(struct ble_gap_event* event, void* arg) {
  auto* transport = static_cast<EcyStatBleTransport*>(arg);
  if (transport == nullptr) {
    return 0;
  }

  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status != 0) {
        transport->complete_operation(false);
        return 0;
      }
      if (transport->pending_operation_ != PendingOperation::Connect) {
        ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
      }
      transport->conn_handle_ = event->connect.conn_handle;
      transport->connected_ = true;
      ESP_LOGI(
          kTag,
          "connected to selected ECY-STAT candidate: conn_handle=%u",
          transport->conn_handle_);
      transport->complete_operation(true);
      return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
      if (event->enc_change.conn_handle != transport->conn_handle_) {
        return 0;
      }
      {
        struct ble_gap_conn_desc connection_description{};
        const int descriptor_result = ble_gap_conn_find(
            event->enc_change.conn_handle,
            &connection_description);
        if (descriptor_result == 0) {
          ESP_LOGI(
              kTag,
              "encryption change: conn_handle=%u status=%d encrypted=%u authenticated=%u bonded=%u key_size=%u",
              event->enc_change.conn_handle,
              event->enc_change.status,
              connection_description.sec_state.encrypted,
              connection_description.sec_state.authenticated,
              connection_description.sec_state.bonded,
              connection_description.sec_state.key_size);
        } else {
          ESP_LOGI(
              kTag,
              "encryption change: conn_handle=%u status=%d descriptor_rc=%d",
              event->enc_change.conn_handle,
              event->enc_change.status,
              descriptor_result);
        }
      }
      if (event->enc_change.status != 0) {
        transport->complete_operation(false);
        return 0;
      }
      if (transport->pending_operation_ == PendingOperation::Read ||
          transport->pending_operation_ == PendingOperation::Write) {
        transport->retry_pending_io();
      }
      return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
      if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
        if (!transport->allow_pairing_input_) {
          g_pending_passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;
          ESP_LOGW(
              kTag,
              "pairing requested while PIN input is disabled; rejecting it");
          transport->fail_pending_operation();
          return 0;
        }
        g_pending_passkey_conn_handle = event->passkey.conn_handle;
        ESP_LOGI(
            kTag,
            "pairing requested: enter the 6-digit ECY-STAT PIN in monitor and press Enter; PIN is not logged");
      } else {
        ESP_LOGW(
            kTag,
            "unsupported pairing IO action=%u",
            static_cast<unsigned>(event->passkey.params.action));
        transport->fail_pending_operation();
      }
      return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
      transport->fail_pending_operation();
      ESP_LOGE(kTag, "Existing bond rejected; deliberate re-commissioning required");
      return BLE_GAP_REPEAT_PAIRING_IGNORE;

    case BLE_GAP_EVENT_DISCONNECT:
      if (event->disconnect.conn.conn_handle != transport->conn_handle_) {
        return 0;
      }
      g_pending_passkey_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      transport->connected_ = false;
      transport->discovered_ = false;
      transport->conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
      if (transport->pending_operation_ != PendingOperation::None) {
        transport->complete_operation(
            transport->pending_operation_ == PendingOperation::Disconnect);
      }
      return 0;

    default:
      return 0;
  }
}

bool EcyStatBleTransport::paired_identity(ble_addr_t& identity) const {
  ble_gap_conn_desc info{};
  if (!connected_ || ble_gap_conn_find(conn_handle_, &info) != 0 ||
      !info.sec_state.encrypted || !info.sec_state.authenticated || !info.sec_state.bonded) {
    return false;
  }
  identity = info.peer_id_addr;
  return true;
}

}  // namespace frost
