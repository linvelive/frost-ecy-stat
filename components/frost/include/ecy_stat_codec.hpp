#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace frost {

constexpr std::size_t kCommandPayloadSize = 8;
constexpr std::size_t kTargetReadbackOffset = 14;
constexpr std::size_t kTargetReadbackSize = 18;
constexpr std::size_t kFanReadbackSize = 3;

inline constexpr char kCommandCharacteristicUuid[] =
    "00000004-0000-0000-0000-00137e5a8eef";
inline constexpr char kTargetStateCharacteristicUuid[] =
    "00020001-0000-0000-0000-00137e5a8eef";
inline constexpr char kFanStateCharacteristicUuid[] =
    "00020004-0000-0000-0000-00137e5a8eef";

enum class FanMode : std::uint8_t {
  Auto,
  On,
};

enum class CodecError : std::uint8_t {
  Ok,
  InvalidTemperature,
  TargetReadbackTooShort,
  FanReadbackWrongLength,
  UnknownFanState,
};

struct ThermostatReadback {
  float target_temperature_f;
  FanMode fan_mode;
};

CodecError encode_target_temperature(
    float temperature_f,
    std::array<std::uint8_t, kCommandPayloadSize>& payload);

CodecError encode_fan_mode(
    FanMode mode,
    std::array<std::uint8_t, kCommandPayloadSize>& payload);

CodecError decode_target_temperature_readback(
    const std::uint8_t* value,
    std::size_t length,
    float& temperature_f);

CodecError decode_fan_mode_readback(
    const std::uint8_t* value,
    std::size_t length,
    FanMode& mode);

CodecError decode_readback(
    const std::uint8_t* target_value,
    std::size_t target_length,
    const std::uint8_t* fan_value,
    std::size_t fan_length,
    ThermostatReadback& readback);

}  // namespace frost
