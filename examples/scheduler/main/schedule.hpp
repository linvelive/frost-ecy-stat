#pragma once
#include "desired_state.hpp"
#include <cstddef>
#include <cstdint>
namespace frost {
struct ScheduleEntry { std::uint8_t hour, minute; DesiredState state; };
struct LocalTime {
  std::uint8_t hour, minute;
  std::int64_t day;  // Consecutive local calendar-day number, not UTC day.
};
class LocalClock {
 public:
  virtual ~LocalClock() = default;
  enum class ReadResult { Ready, Unavailable };
  virtual ReadResult read(LocalTime&) = 0;
};
class ScheduleApplier {
 public:
  virtual ~ScheduleApplier() = default;
  virtual bool ensure_applied(const DesiredState&) = 0;
};
enum class ScheduleTickResult { Applied, NoOp, ApplyFailed, ClockUnavailable, InvalidTime, InvalidSchedule };
class ScheduleRunner {
 public:
  // Sorted, unique times; entries must remain immutable and outlive the runner.
  ScheduleRunner(LocalClock&, ScheduleApplier&, const ScheduleEntry*, std::size_t);
  bool valid() const { return valid_; }
  ScheduleTickResult tick();
 private:
  LocalClock& clock_;
  ScheduleApplier& applier_;
  const ScheduleEntry* entries_;
  std::size_t count_;
  bool valid_ = false, applied_ = false;
  std::size_t last_index_ = 0;
  std::int64_t last_day_ = 0;
};
}  // namespace frost
