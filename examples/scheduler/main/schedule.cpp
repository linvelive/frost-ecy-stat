#include "schedule.hpp"
#include <cmath>
namespace frost {
ScheduleRunner::ScheduleRunner(LocalClock& clock, ScheduleApplier& applier,
    const ScheduleEntry* entries, std::size_t count)
    : clock_(clock), applier_(applier), entries_(entries), count_(count) {
  if (entries == nullptr || count == 0) return;
  unsigned previous = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& e = entries[i];
    const unsigned minutes = e.hour * 60U + e.minute;
    if (e.hour >= 24 || e.minute >= 60 || (i != 0 && minutes <= previous) ||
        !std::isfinite(e.state.target_temperature_f) ||
        (e.state.fan_mode != FanMode::On && e.state.fan_mode != FanMode::Auto)) return;
    previous = minutes;
  }
  valid_ = true;
}
ScheduleTickResult ScheduleRunner::tick() {
  if (!valid_) return ScheduleTickResult::InvalidSchedule;
  LocalTime now{};
  if (clock_.read(now) != LocalClock::ReadResult::Ready) return ScheduleTickResult::ClockUnavailable;
  if (now.hour >= 24 || now.minute >= 60) return ScheduleTickResult::InvalidTime;
  const unsigned minutes = now.hour * 60U + now.minute;
  std::size_t selected = count_ - 1;
  std::int64_t day = now.day - 1;
  for (std::size_t i = 0; i < count_; ++i) {
    if (entries_[i].hour * 60U + entries_[i].minute > minutes) break;
    selected = i; day = now.day;
  }
  if (applied_ && selected == last_index_ && day == last_day_) return ScheduleTickResult::NoOp;
  if (!applier_.ensure_applied(entries_[selected].state)) return ScheduleTickResult::ApplyFailed;
  applied_ = true; last_index_ = selected; last_day_ = day;
  return ScheduleTickResult::Applied;
}
}  // namespace frost
