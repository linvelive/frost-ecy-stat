#include "ecy_stat_controller.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
using frost::ApplyStatus;
using frost::BleTransport;
using frost::FanMode;
using frost::FieldApplyStatus;
namespace frost {
constexpr DesiredState kMorningState{68.0f, FanMode::Auto};
constexpr DesiredState kNightState{66.0f, FanMode::On};
}
void expect_bytes(
    const std::array<std::uint8_t, frost::kCommandPayloadSize>& actual,
    const char* expected_hex) {
  const std::array<std::uint8_t, frost::kCommandPayloadSize> expected = [&] {
    std::array<std::uint8_t, frost::kCommandPayloadSize> bytes{};
    const char* cursor = expected_hex;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      unsigned int value = 0;
      assert(std::sscanf(cursor, "%2x", &value) == 1);
      bytes[i] = static_cast<std::uint8_t>(value);
      cursor += 2;
    }
    return bytes;
  }();
  assert(actual == expected);
}

void expect_vector_bytes(
    const std::vector<std::uint8_t>& actual,
    const char* expected_hex) {
  assert(actual.size() == frost::kCommandPayloadSize);
  std::array<std::uint8_t, frost::kCommandPayloadSize> as_array{};
  std::copy(actual.begin(), actual.end(), as_array.begin());
  expect_bytes(as_array, expected_hex);
}

class RecordingTransport final : public BleTransport {
 public:
  enum class Operation { Connect, Discover, Write, Read, Delay, Disconnect };

  RecordingTransport()
      : target_value(default_target_value()),
        fan_value({0x01, 0x00, 0x00}) {}

  bool connect() override {
    operations.push_back(Operation::Connect);
    return true;
  }

  bool discover() override {
    operations.push_back(Operation::Discover);
    return true;
  }

  bool write_with_response(
      const char* characteristic_uuid,
      const std::uint8_t* payload,
      std::size_t length) override {
    operations.push_back(Operation::Write);
    uuids.emplace_back(characteristic_uuid);
    writes.emplace_back(payload, payload + length);
    const bool successful = write_index >= write_results.size() ||
                            write_results[write_index];
    ++write_index;
    return successful;
  }

  bool read(
      const char* characteristic_uuid,
      std::uint8_t* output,
      std::size_t capacity,
      std::size_t& length) override {
    operations.push_back(Operation::Read);
    const bool successful = read_index >= read_results.size() ||
                            read_results[read_index];
    ++read_index;
    if (!successful) {
      return false;
    }

    if (std::string(characteristic_uuid) ==
        frost::kTargetStateCharacteristicUuid) {
      const auto& value = target_readbacks.empty()
                              ? target_value
                              : target_readbacks[std::min(
                                    target_read_index,
                                    target_readbacks.size() - 1)];
      ++target_read_index;
      assert(value.size() <= capacity);
      std::memcpy(output, value.data(), value.size());
      length = value.size();
      return true;
    }
    assert(std::string(characteristic_uuid) ==
           frost::kFanStateCharacteristicUuid);
    const auto& value = fan_readbacks.empty()
                            ? fan_value
                            : fan_readbacks[std::min(
                                  fan_read_index,
                                  fan_readbacks.size() - 1)];
    ++fan_read_index;
    assert(value.size() <= capacity);
    std::memcpy(output, value.data(), value.size());
    length = value.size();
    return true;
  }

  void delay_ms(std::uint32_t milliseconds) override {
    operations.push_back(Operation::Delay);
    delays_ms.push_back(milliseconds);
  }

  void disconnect() override {
    operations.push_back(Operation::Disconnect);
  }

  std::vector<Operation> operations;
  std::vector<std::string> uuids;
  std::vector<std::vector<std::uint8_t>> writes;
  std::vector<bool> write_results;
  std::vector<bool> read_results;
  std::vector<std::uint32_t> delays_ms;
  std::vector<std::array<std::uint8_t, 18>> target_readbacks;
  std::vector<std::array<std::uint8_t, 3>> fan_readbacks;
  std::array<std::uint8_t, 18> target_value;
  std::array<std::uint8_t, 3> fan_value;

 private:
  static std::array<std::uint8_t, 18> default_target_value() {
    std::array<std::uint8_t, 18> value{};
    value[14] = 0x00;
    value[15] = 0x00;
    value[16] = 0x88;
    value[17] = 0x42;
    return value;
  }

  std::size_t write_index = 0;
  std::size_t read_index = 0;
  std::size_t target_read_index = 0;
  std::size_t fan_read_index = 0;
};

void test_codec_vectors() {
  std::array<std::uint8_t, frost::kCommandPayloadSize> payload{};
  assert(frost::encode_target_temperature(66.0f, payload) ==
         frost::CodecError::Ok);
  expect_bytes(payload, "00020e0000008442");

  assert(frost::encode_target_temperature(68.0f, payload) ==
         frost::CodecError::Ok);
  expect_bytes(payload, "00020e0000008842");

  assert(frost::encode_fan_mode(FanMode::Auto, payload) ==
         frost::CodecError::Ok);
  expect_bytes(payload, "0302000000000000");

  assert(frost::encode_fan_mode(FanMode::On, payload) ==
         frost::CodecError::Ok);
  expect_bytes(payload, "0302010000000000");

  float target = 0.0f;
  std::array<std::uint8_t, 18> readback{};
  readback[14] = 0x00;
  readback[15] = 0x00;
  readback[16] = 0x84;
  readback[17] = 0x42;
  assert(frost::decode_target_temperature_readback(
             readback.data(), readback.size(), target) ==
         frost::CodecError::Ok);
  assert(target == 66.0f);

  FanMode mode = FanMode::On;
  const std::array<std::uint8_t, 3> fan_auto = {0x01, 0x00, 0x00};
  assert(frost::decode_fan_mode_readback(
             fan_auto.data(), fan_auto.size(), mode) == frost::CodecError::Ok);
  assert(mode == FanMode::Auto);
}

std::array<std::uint8_t, 18> target_readback_bits(std::uint32_t bits) {
  std::array<std::uint8_t, 18> value{};
  for (std::size_t i = 0; i < 4; ++i) {
    value[14 + i] = static_cast<std::uint8_t>(bits >> (8 * i));
  }
  return value;
}

void test_controller_requires_exact_target() {
  const auto exact = target_readback_bits(0x42880000U);  // 68.0F
  // Adjacent floats below/above 68F differ by less than the old 0.001F tolerance.
  for (const auto bits : {0x4287ffffU, 0x42880001U}) {
    const auto near = target_readback_bits(bits);
    {
      RecordingTransport transport;
      transport.target_value = near;
      frost::EcyStatController controller(transport);
      const auto result = controller.verify_current(frost::kMorningState);
      assert(result.status == ApplyStatus::Failed);
      assert(result.target.status == FieldApplyStatus::Mismatch);
      assert(transport.writes.empty());
    }
    {
      RecordingTransport transport;
      transport.target_readbacks = {near, exact, exact};
      frost::EcyStatController controller(transport);
      const auto result = controller.ensure_applied(frost::kMorningState);
      assert(result.status == ApplyStatus::Verified);
      assert(transport.writes.size() == 2);
    }
    {
      RecordingTransport transport;
      transport.target_value = near;
      frost::EcyStatController controller(transport);
      const auto result = controller.apply(frost::kMorningState);
      assert(result.status == ApplyStatus::Failed);
      assert(result.target.status == FieldApplyStatus::Mismatch);
      assert(result.fan.status == FieldApplyStatus::NotAttempted);
      assert(transport.writes.size() == 1);
    }
    {
      RecordingTransport transport;
      transport.target_readbacks = {exact, near};
      frost::EcyStatController controller(transport);
      const auto result = controller.apply(frost::kMorningState);
      assert(result.status == ApplyStatus::PartialFailure);
      assert(result.target.status == FieldApplyStatus::Mismatch);
      assert(result.fan.status == FieldApplyStatus::Verified);
      assert(transport.writes.size() == 2);
    }
  }

  RecordingTransport transport;
  transport.target_value = target_readback_bits(0x42890000U);  // 68.5F
  frost::EcyStatController controller(transport);
  auto desired = frost::kMorningState;
  desired.target_temperature_f = 68.5f;
  assert(controller.ensure_applied(desired).status == ApplyStatus::Verified);
  assert(transport.writes.empty());
  assert(controller.apply(desired).status == ApplyStatus::Verified);
  assert(transport.writes.size() == 2);
}

void test_nonfinite_target_readbacks_fail_closed() {
  // Quiet NaN, positive infinity, negative infinity in IEEE-754 binary32.
  for (const auto bits : {0x7fc00000U, 0x7f800000U, 0xff800000U}) {
    const auto invalid = target_readback_bits(bits);
    float decoded = 0.0f;
    assert(frost::decode_target_temperature_readback(
               invalid.data(), invalid.size(), decoded) ==
           frost::CodecError::InvalidTemperature);
    {
      RecordingTransport transport;
      transport.target_value = invalid;
      frost::EcyStatController controller(transport);
      const auto result = controller.ensure_applied(frost::kMorningState);
      assert(result.status == ApplyStatus::Failed);
      assert(result.target.status == FieldApplyStatus::ReadbackMalformed);
      assert(transport.writes.empty());
    }
    {
      RecordingTransport transport;
      transport.target_value = invalid;
      frost::EcyStatController controller(transport);
      const auto result = controller.apply(frost::kMorningState);
      assert(result.status == ApplyStatus::Failed);
      assert(result.target.status == FieldApplyStatus::ReadbackMalformed);
      assert(result.fan.status == FieldApplyStatus::NotAttempted);
      assert(transport.writes.size() == 1);
    }
    {
      RecordingTransport transport;
      transport.target_readbacks = {transport.target_value, invalid};
      frost::EcyStatController controller(transport);
      const auto result = controller.apply(frost::kMorningState);
      assert(result.status == ApplyStatus::PartialFailure);
      assert(result.target.status == FieldApplyStatus::ReadbackMalformed);
      assert(result.fan.status == FieldApplyStatus::Verified);
      assert(transport.writes.size() == 2);
    }
  }
}

void test_nonfinite_target_commands_do_not_connect() {
  for (const auto value : {std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity(),
                           -std::numeric_limits<float>::infinity()}) {
    RecordingTransport transport;
    frost::EcyStatController controller(transport);
    auto desired = frost::kMorningState;
    desired.target_temperature_f = value;
    assert(controller.verify_current(desired).status == ApplyStatus::InvalidCommand);
    assert(controller.ensure_applied(desired).status == ApplyStatus::InvalidCommand);
    assert(controller.apply(desired).status == ApplyStatus::InvalidCommand);
    assert(transport.operations.empty());
  }
}


void test_controller_ensure_skips_write_when_state_matches() {
  RecordingTransport transport;
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result =
      controller.ensure_applied(frost::kMorningState);

  assert(result.status == ApplyStatus::Verified);
  assert(result.target.status == FieldApplyStatus::Verified);
  assert(result.fan.status == FieldApplyStatus::Verified);
  assert(transport.writes.empty());
  assert(transport.operations.size() == 5);
  assert(transport.operations[2] == RecordingTransport::Operation::Read);
  assert(transport.operations[3] == RecordingTransport::Operation::Read);
  assert(transport.operations[4] == RecordingTransport::Operation::Disconnect);
}
void test_controller_ensure_applies_after_confirmed_mismatch() {
  RecordingTransport transport;
  std::array<std::uint8_t, 18> current_target = transport.target_value;
  current_target[16] = 0x84;
  std::array<std::uint8_t, 18> desired_target = transport.target_value;
  transport.target_readbacks = {
      current_target,
      desired_target,
      desired_target,
  };
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result =
      controller.ensure_applied(frost::kMorningState);

  assert(result.status == ApplyStatus::Verified);
  assert(result.target.status == FieldApplyStatus::Verified);
  assert(result.fan.status == FieldApplyStatus::Verified);
  assert(transport.writes.size() == 2);
}
void test_controller_ensure_fails_closed_on_read_error() {
  RecordingTransport transport;
  transport.read_results = {false};
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result =
      controller.ensure_applied(frost::kMorningState);

  assert(result.status == ApplyStatus::Failed);
  assert(result.target.status == FieldApplyStatus::ReadFailed);
  assert(result.fan.status == FieldApplyStatus::NotAttempted);
  assert(transport.writes.empty());
}
void test_controller_verifies_after_ordered_io() {
  RecordingTransport transport;
  frost::EcyStatController controller(transport);
  const frost::ApplyResult result = controller.apply(frost::kMorningState);

  assert(result.status == ApplyStatus::Verified);
  assert(result.target.status == FieldApplyStatus::Verified);
  assert(result.target.readback_temperature_f == 68.0f);
  assert(result.fan.status == FieldApplyStatus::Verified);
  assert(result.fan.readback_mode == FanMode::Auto);
  assert(transport.operations.size() == 8);
  assert(transport.operations[0] == RecordingTransport::Operation::Connect);
  assert(transport.operations[1] == RecordingTransport::Operation::Discover);
  assert(transport.operations[2] == RecordingTransport::Operation::Write);
  assert(transport.operations[3] == RecordingTransport::Operation::Read);
  assert(transport.operations[4] == RecordingTransport::Operation::Write);
  assert(transport.operations[5] == RecordingTransport::Operation::Read);
  assert(transport.operations[6] ==
         RecordingTransport::Operation::Read);
  assert(transport.operations[7] ==
         RecordingTransport::Operation::Disconnect);
  expect_vector_bytes(transport.writes[0], "00020e0000008842");
  expect_vector_bytes(transport.writes[1], "0302000000000000");
}
void test_controller_stops_before_fan_when_target_mismatches() {
  RecordingTransport transport;
  transport.target_value[16] = 0x84;
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result = controller.apply(frost::kMorningState);

  assert(result.status == ApplyStatus::Failed);
  assert(result.target.status == FieldApplyStatus::Mismatch);
  assert(result.fan.status == FieldApplyStatus::NotAttempted);
  assert(transport.writes.size() == 1);
  assert(transport.operations.size() == 5);
  assert(transport.operations[2] == RecordingTransport::Operation::Write);
  assert(transport.operations[3] == RecordingTransport::Operation::Read);
}
void test_controller_reports_verified_target_and_failed_fan_write() {
  RecordingTransport transport;
  transport.write_results = {true, false};
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result = controller.apply(frost::kMorningState);

  assert(result.status == ApplyStatus::PartialFailure);
  assert(result.target.status == FieldApplyStatus::Verified);
  assert(result.target.readback_temperature_f == 68.0f);
  assert(result.fan.status == FieldApplyStatus::WriteFailed);
  assert(transport.writes.size() == 2);
  assert(transport.operations.size() == 6);
  assert(transport.operations[2] == RecordingTransport::Operation::Write);
  assert(transport.operations[3] == RecordingTransport::Operation::Read);
  assert(transport.operations[4] == RecordingTransport::Operation::Write);
}
void test_controller_compares_fan_after_target_verification() {
  RecordingTransport transport;
  transport.fan_value = {0x00, 0x01, 0x00};
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result = controller.apply(frost::kMorningState);

  assert(result.status == ApplyStatus::PartialFailure);
  assert(result.target.status == FieldApplyStatus::Verified);
  assert(result.fan.status == FieldApplyStatus::Timeout);
  assert(result.fan.readback_mode == FanMode::On);
  assert(transport.delays_ms == std::vector<std::uint32_t>({1000, 2000}));
}
void test_controller_waits_for_delayed_fan_convergence() {
  RecordingTransport transport;
  transport.target_value[16] = 0x84;
  transport.fan_readbacks = {
      {0x01, 0x00, 0x00},
      {0x01, 0x00, 0x00},
      {0x00, 0x01, 0x00},
  };
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result = controller.apply(frost::kNightState);

  assert(result.status == ApplyStatus::Verified);
  assert(result.target.status == FieldApplyStatus::Verified);
  assert(result.target.readback_temperature_f == 66.0f);
  assert(result.fan.status == FieldApplyStatus::Verified);
  assert(result.fan.readback_mode == FanMode::On);
  assert(transport.delays_ms == std::vector<std::uint32_t>({1000, 2000}));
  assert(transport.operations.back() == RecordingTransport::Operation::Disconnect);
  expect_vector_bytes(transport.writes[0], "00020e0000008442");
  expect_vector_bytes(transport.writes[1], "0302010000000000");
}
void test_controller_retries_transient_unknown_fan_readback() {
  RecordingTransport transport;
  transport.target_value[16] = 0x84;
  transport.fan_readbacks = {
      {0x00, 0x00, 0x00},
      {0x01, 0x00, 0x00},
      {0x00, 0x01, 0x00},
  };
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result = controller.apply(frost::kNightState);

  assert(result.status == ApplyStatus::Verified);
  assert(result.target.status == FieldApplyStatus::Verified);
  assert(result.fan.status == FieldApplyStatus::Verified);
  assert(result.fan.readback_mode == FanMode::On);
  assert(transport.delays_ms == std::vector<std::uint32_t>({1000, 2000}));
}
void test_controller_detects_target_drift_after_fan_convergence() {
  RecordingTransport transport;
  std::array<std::uint8_t, 18> initial_target = transport.target_value;
  initial_target[16] = 0x84;
  std::array<std::uint8_t, 18> drifted_target = initial_target;
  drifted_target[16] = 0x88;
  transport.target_readbacks = {initial_target, drifted_target};
  transport.fan_readbacks = {{0x00, 0x01, 0x00}};
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result = controller.apply(frost::kNightState);

  assert(result.status == ApplyStatus::PartialFailure);
  assert(result.target.status == FieldApplyStatus::Mismatch);
  assert(result.target.readback_temperature_f == 68.0f);
  assert(result.fan.status == FieldApplyStatus::Verified);
}
void test_controller_reports_fan_timeout_as_partial_failure() {
  RecordingTransport transport;
  transport.target_value[16] = 0x84;
  transport.fan_readbacks = {
      {0x01, 0x00, 0x00},
      {0x01, 0x00, 0x00},
      {0x01, 0x00, 0x00},
  };
  frost::EcyStatController controller(transport);

  const frost::ApplyResult result = controller.apply(frost::kNightState);

  assert(result.status == ApplyStatus::PartialFailure);
  assert(result.target.status == FieldApplyStatus::Verified);
  assert(result.fan.status == FieldApplyStatus::Timeout);
  assert(result.fan.readback_mode == FanMode::Auto);
  assert(transport.delays_ms == std::vector<std::uint32_t>({1000, 2000}));
}

void test_invalid_values_never_verify() {
  RecordingTransport transport;
  frost::EcyStatController controller(transport);
  const frost::DesiredState invalid{std::numeric_limits<float>::quiet_NaN(), FanMode::Auto};
  assert(controller.ensure_applied(invalid).status == ApplyStatus::InvalidCommand);
  assert(transport.operations.empty());
  assert(controller.apply({68, static_cast<FanMode>(99)}).status == ApplyStatus::InvalidCommand);
  assert(transport.operations.empty());
  transport.target_value[14] = 0; transport.target_value[15] = 0;
  transport.target_value[16] = 0xc0; transport.target_value[17] = 0x7f;
  auto result = controller.ensure_applied(frost::kMorningState);
  assert(result.status != ApplyStatus::Verified);
  assert(result.target.status == FieldApplyStatus::ReadbackMalformed);
  assert(transport.writes.empty());
  transport.target_value[16] = 0x80; // positive infinity
  assert(controller.ensure_applied(frost::kMorningState).target.status == FieldApplyStatus::ReadbackMalformed);
  assert(transport.writes.empty());
}

int main() {
  test_controller_requires_exact_target();
  test_nonfinite_target_readbacks_fail_closed();
  test_nonfinite_target_commands_do_not_connect();
  test_invalid_values_never_verify();
  test_codec_vectors();
  test_controller_ensure_skips_write_when_state_matches();
  test_controller_ensure_applies_after_confirmed_mismatch();
  test_controller_ensure_fails_closed_on_read_error();
  test_controller_verifies_after_ordered_io();
  test_controller_stops_before_fan_when_target_mismatches();
  test_controller_reports_verified_target_and_failed_fan_write();
  test_controller_compares_fan_after_target_verification();
  test_controller_waits_for_delayed_fan_convergence();
  test_controller_retries_transient_unknown_fan_readback();
  test_controller_detects_target_drift_after_fan_convergence();
  test_controller_reports_fan_timeout_as_partial_failure();
}
