#include "ecy_stat_codec.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace frost {
namespace {

static_assert(sizeof(float) == sizeof(std::uint32_t),
              "ECY-STAT uses four-byte IEEE-754 float values");

constexpr std::uint8_t kTemperaturePrefix[] = {0x00, 0x02, 0x0e, 0x00};
constexpr std::uint8_t kFanPrefix[] = {0x03, 0x02};

std::uint32_t float_bits(float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "unexpected float width");
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

float bits_float(std::uint32_t bits) {
  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

void write_little_endian_float(float value, std::uint8_t* output) {
  const std::uint32_t bits = float_bits(value);
  output[0] = static_cast<std::uint8_t>(bits & 0xffU);
  output[1] = static_cast<std::uint8_t>((bits >> 8U) & 0xffU);
  output[2] = static_cast<std::uint8_t>((bits >> 16U) & 0xffU);
  output[3] = static_cast<std::uint8_t>((bits >> 24U) & 0xffU);
}

float read_little_endian_float(const std::uint8_t* input) {
  const std::uint32_t bits =
      static_cast<std::uint32_t>(input[0]) |
      (static_cast<std::uint32_t>(input[1]) << 8U) |
      (static_cast<std::uint32_t>(input[2]) << 16U) |
      (static_cast<std::uint32_t>(input[3]) << 24U);
  return bits_float(bits);
}

}  // namespace

CodecError encode_target_temperature(
    float temperature_f,
    std::array<std::uint8_t, kCommandPayloadSize>& payload) {
  // The device's accepted range, increments, and clamping rules are unknown.
  // Only reject non-finite values that cannot represent a real target.
  if (!std::isfinite(temperature_f)) {
    return CodecError::InvalidTemperature;
  }

  for (std::size_t i = 0; i < sizeof(kTemperaturePrefix); ++i) {
    payload[i] = kTemperaturePrefix[i];
  }
  write_little_endian_float(temperature_f, payload.data() + 4);
  return CodecError::Ok;
}

CodecError encode_fan_mode(
    FanMode mode,
    std::array<std::uint8_t, kCommandPayloadSize>& payload) {
  if (mode != FanMode::Auto && mode != FanMode::On) return CodecError::UnknownFanState;
  payload.fill(0);
  payload[0] = kFanPrefix[0];
  payload[1] = kFanPrefix[1];
  payload[2] = mode == FanMode::Auto ? 0x00 : 0x01;
  return CodecError::Ok;
}

CodecError decode_target_temperature_readback(
    const std::uint8_t* value,
    std::size_t length,
    float& temperature_f) {
  if (value == nullptr || length < kTargetReadbackSize) {
    return CodecError::TargetReadbackTooShort;
  }
  temperature_f = read_little_endian_float(value + kTargetReadbackOffset);
  return std::isfinite(temperature_f) ? CodecError::Ok : CodecError::InvalidTemperature;
}

CodecError decode_fan_mode_readback(
    const std::uint8_t* value,
    std::size_t length,
    FanMode& mode) {
  if (value == nullptr || length != kFanReadbackSize) {
    return CodecError::FanReadbackWrongLength;
  }

  if (value[0] == 0x00 && value[1] == 0x01 && value[2] == 0x00) {
    mode = FanMode::On;
    return CodecError::Ok;
  }
  if (value[0] == 0x01 && value[1] == 0x00 && value[2] == 0x00) {
    mode = FanMode::Auto;
    return CodecError::Ok;
  }
  return CodecError::UnknownFanState;
}

CodecError decode_readback(
    const std::uint8_t* target_value,
    std::size_t target_length,
    const std::uint8_t* fan_value,
    std::size_t fan_length,
    ThermostatReadback& readback) {
  CodecError error = decode_target_temperature_readback(
      target_value, target_length, readback.target_temperature_f);
  if (error != CodecError::Ok) {
    return error;
  }
  return decode_fan_mode_readback(fan_value, fan_length, readback.fan_mode);
}

}  // namespace frost
