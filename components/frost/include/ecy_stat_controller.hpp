#pragma once

#include "ecy_stat_codec.hpp"
#include "desired_state.hpp"

#include <cstddef>
#include <cstdint>

namespace frost {

class BleTransport {
 public:
  virtual ~BleTransport() = default;

  virtual bool connect() = 0;
  virtual bool discover() = 0;
  virtual bool write_with_response(
      const char* characteristic_uuid,
      const std::uint8_t* payload,
      std::size_t length) = 0;
  virtual bool read(
      const char* characteristic_uuid,
      std::uint8_t* output,
      std::size_t capacity,
      std::size_t& length) = 0;
  virtual void delay_ms(std::uint32_t milliseconds) = 0;
  virtual void disconnect() = 0;
};

enum class ApplyStatus : std::uint8_t {
  Verified,
  InvalidCommand,
  ConnectionFailed,
  DiscoveryFailed,
  Failed,
  PartialFailure,
};

enum class FieldApplyStatus : std::uint8_t {
  NotAttempted,
  Verified,
  WriteFailed,
  ReadFailed,
  ReadbackMalformed,
  Mismatch,
  Timeout,
};

struct TargetApplyResult {
  FieldApplyStatus status;
  float readback_temperature_f;
};

struct FanApplyResult {
  FieldApplyStatus status;
  FanMode readback_mode;
};

struct ApplyResult {
  ApplyStatus status;
  TargetApplyResult target;
  FanApplyResult fan;
};

class EcyStatController {
 public:
  explicit EcyStatController(BleTransport& transport);

  ApplyResult verify_current(const DesiredState& desired_state);
  ApplyResult ensure_applied(const DesiredState& desired_state);
  ApplyResult apply(const DesiredState& desired_state);

 private:
  BleTransport& transport_;
};

}  // namespace frost
