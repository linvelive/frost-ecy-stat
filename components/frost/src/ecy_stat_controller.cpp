#include "ecy_stat_controller.hpp"

#include <array>
#include <cmath>

namespace frost {
namespace {

constexpr std::size_t kReadBufferCapacity = 32;
constexpr std::array<std::uint32_t, 3> kFanReadbackPollDelaysMs = {
    0,
    1000,
    2000,
};

ApplyResult result(ApplyStatus status) {
  return {
      status,
      {FieldApplyStatus::NotAttempted, 0.0f},
      {FieldApplyStatus::NotAttempted, FanMode::Auto},
  };
}

bool is_comparable(FieldApplyStatus status) {
  return status == FieldApplyStatus::Verified ||
         status == FieldApplyStatus::Mismatch;
}

}  // namespace

EcyStatController::EcyStatController(BleTransport& transport)
    : transport_(transport) {}

ApplyResult EcyStatController::verify_current(
    const DesiredState& desired_state) {
  if (!std::isfinite(desired_state.target_temperature_f) ||
      (desired_state.fan_mode != FanMode::Auto && desired_state.fan_mode != FanMode::On)) {
    return result(ApplyStatus::InvalidCommand);
  }
  ApplyResult final_result = result(ApplyStatus::ConnectionFailed);

  if (!transport_.connect()) {
    return final_result;
  }

  const auto finish = [&](ApplyResult completed) {
    transport_.disconnect();
    return completed;
  };

  if (!transport_.discover()) {
    final_result = result(ApplyStatus::DiscoveryFailed);
    return finish(final_result);
  }

  std::array<std::uint8_t, kReadBufferCapacity> target_value{};
  std::size_t target_length = 0;
  if (!transport_.read(
          kTargetStateCharacteristicUuid,
          target_value.data(),
          target_value.size(),
          target_length)) {
    final_result = result(ApplyStatus::Failed);
    final_result.target.status = FieldApplyStatus::ReadFailed;
    return finish(final_result);
  }

  if (decode_target_temperature_readback(
          target_value.data(),
          target_length,
          final_result.target.readback_temperature_f) != CodecError::Ok) {
    final_result = result(ApplyStatus::Failed);
    final_result.target.status = FieldApplyStatus::ReadbackMalformed;
    return finish(final_result);
  }

  final_result.target.status = FieldApplyStatus::Verified;
  if (final_result.target.readback_temperature_f !=
      desired_state.target_temperature_f) {
    final_result.target.status = FieldApplyStatus::Mismatch;
  }

  std::array<std::uint8_t, kReadBufferCapacity> fan_value{};
  std::size_t fan_length = 0;
  if (!transport_.read(
          kFanStateCharacteristicUuid,
          fan_value.data(),
          fan_value.size(),
          fan_length)) {
    final_result.status = ApplyStatus::Failed;
    final_result.fan.status = FieldApplyStatus::ReadFailed;
    return finish(final_result);
  }

  if (decode_fan_mode_readback(
          fan_value.data(),
          fan_length,
          final_result.fan.readback_mode) != CodecError::Ok) {
    final_result.status = ApplyStatus::Failed;
    final_result.fan.status = FieldApplyStatus::ReadbackMalformed;
    return finish(final_result);
  }

  final_result.fan.status = FieldApplyStatus::Verified;
  if (final_result.fan.readback_mode != desired_state.fan_mode) {
    final_result.fan.status = FieldApplyStatus::Mismatch;
  }

  final_result.status =
      final_result.target.status == FieldApplyStatus::Verified &&
              final_result.fan.status == FieldApplyStatus::Verified
          ? ApplyStatus::Verified
          : ApplyStatus::Failed;
  return finish(final_result);
}

ApplyResult EcyStatController::ensure_applied(
    const DesiredState& desired_state) {
  const ApplyResult current = verify_current(desired_state);
  if (current.status == ApplyStatus::Verified) {
    return current;
  }

  // Only a complete, valid read that proves a mismatch authorizes writes.
  // A transport/readback error fails closed instead of guessing the state.
  if (!is_comparable(current.target.status) ||
      !is_comparable(current.fan.status)) {
    return current;
  }

  return apply(desired_state);
}

ApplyResult EcyStatController::apply(const DesiredState& desired_state) {
  std::array<std::uint8_t, kCommandPayloadSize> target_payload{};
  std::array<std::uint8_t, kCommandPayloadSize> fan_payload{};

  if (encode_target_temperature(
          desired_state.target_temperature_f, target_payload) !=
          CodecError::Ok ||
      encode_fan_mode(desired_state.fan_mode, fan_payload) != CodecError::Ok) {
    return result(ApplyStatus::InvalidCommand);
  }

  ApplyResult final_result = result(ApplyStatus::ConnectionFailed);

  if (!transport_.connect()) {
    return final_result;
  }

  const auto finish = [&](ApplyResult completed) {
    transport_.disconnect();
    return completed;
  };

  if (!transport_.discover()) {
    final_result = result(ApplyStatus::DiscoveryFailed);
    return finish(final_result);
  }

  if (!transport_.write_with_response(
          kCommandCharacteristicUuid,
          target_payload.data(),
          target_payload.size())) {
    final_result = result(ApplyStatus::Failed);
    final_result.target.status = FieldApplyStatus::WriteFailed;
    return finish(final_result);
  }

  std::array<std::uint8_t, kReadBufferCapacity> target_value{};
  std::size_t target_length = 0;

  if (!transport_.read(
          kTargetStateCharacteristicUuid,
          target_value.data(),
          target_value.size(),
          target_length)) {
    final_result = result(ApplyStatus::Failed);
    final_result.target.status = FieldApplyStatus::ReadFailed;
    return finish(final_result);
  }

  if (decode_target_temperature_readback(
          target_value.data(),
          target_length,
          final_result.target.readback_temperature_f) != CodecError::Ok) {
    final_result = result(ApplyStatus::Failed);
    final_result.target.status = FieldApplyStatus::ReadbackMalformed;
    return finish(final_result);
  }

  if (final_result.target.readback_temperature_f !=
      desired_state.target_temperature_f) {
    final_result.status = ApplyStatus::Failed;
    final_result.target.status = FieldApplyStatus::Mismatch;
    return finish(final_result);
  }
  final_result.target.status = FieldApplyStatus::Verified;

  if (!transport_.write_with_response(
          kCommandCharacteristicUuid,
          fan_payload.data(),
          fan_payload.size())) {
    final_result.status = ApplyStatus::PartialFailure;
    final_result.fan.status = FieldApplyStatus::WriteFailed;
    return finish(final_result);
  }

  std::array<std::uint8_t, kReadBufferCapacity> fan_value{};
  bool fan_verified = false;
  for (const std::uint32_t delay_ms : kFanReadbackPollDelaysMs) {
    if (delay_ms != 0) {
      transport_.delay_ms(delay_ms);
    }

    std::size_t fan_length = 0;
    if (!transport_.read(
            kFanStateCharacteristicUuid,
            fan_value.data(),
            fan_value.size(),
            fan_length)) {
      final_result.status = ApplyStatus::PartialFailure;
      final_result.fan.status = FieldApplyStatus::ReadFailed;
      return finish(final_result);
    }

    if (decode_fan_mode_readback(
            fan_value.data(),
            fan_length,
            final_result.fan.readback_mode) != CodecError::Ok) {
      if (delay_ms == kFanReadbackPollDelaysMs.back()) {
        final_result.status = ApplyStatus::PartialFailure;
        final_result.fan.status = FieldApplyStatus::ReadbackMalformed;
        return finish(final_result);
      }
      continue;
    }

    if (final_result.fan.readback_mode == desired_state.fan_mode) {
      fan_verified = true;
      break;
    }
  }

  if (!fan_verified) {
    final_result.status = ApplyStatus::PartialFailure;
    final_result.fan.status = FieldApplyStatus::Timeout;
    return finish(final_result);
  }

  final_result.fan.status = FieldApplyStatus::Verified;

  // Fan application is not atomic with Target. Confirm that the earlier
  // Target verification still holds after Fan convergence.
  target_value.fill(0);
  target_length = 0;
  if (!transport_.read(
          kTargetStateCharacteristicUuid,
          target_value.data(),
          target_value.size(),
          target_length)) {
    final_result.status = ApplyStatus::PartialFailure;
    final_result.target.status = FieldApplyStatus::ReadFailed;
    return finish(final_result);
  }

  if (decode_target_temperature_readback(
          target_value.data(),
          target_length,
          final_result.target.readback_temperature_f) != CodecError::Ok) {
    final_result.status = ApplyStatus::PartialFailure;
    final_result.target.status = FieldApplyStatus::ReadbackMalformed;
    return finish(final_result);
  }

  if (final_result.target.readback_temperature_f !=
      desired_state.target_temperature_f) {
    final_result.status = ApplyStatus::PartialFailure;
    final_result.target.status = FieldApplyStatus::Mismatch;
    return finish(final_result);
  }

  final_result.target.status = FieldApplyStatus::Verified;
  final_result.status = ApplyStatus::Verified;
  return finish(final_result);
}

}  // namespace frost
