#pragma once
#include "ecy_stat_codec.hpp"
namespace frost {
struct DesiredState {
  float target_temperature_f;
  FanMode fan_mode;
};
}  // namespace frost
