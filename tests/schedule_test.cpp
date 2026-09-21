#include "schedule.hpp"
#include "clock.hpp"
#include <cassert>
#include <limits>
#include <utility>
#include <initializer_list>
using namespace frost;
struct Clock : LocalClock {
  LocalTime value{10, 0, 100}; bool ready = true;
  ReadResult read(LocalTime& t) override { t = value; return ready ? ReadResult::Ready : ReadResult::Unavailable; }
};
struct Applier : ScheduleApplier {
  unsigned calls = 0; bool succeeds = true; DesiredState last{};
  bool ensure_applied(const DesiredState& s) override { ++calls; last = s; return succeeds; }
};
struct TimeSource : NetworkTimeSource {
  bool ready = false; std::time_t value = 0;
  bool is_synchronized() const override { return ready; }
  std::time_t utc_now() const override { return value; }
};
int main() {
  const ScheduleEntry entries[] = {{8,0,{68,FanMode::Auto}}, {12,30,{67,FanMode::Auto}}, {23,0,{66,FanMode::On}}};
  Clock c; Applier a; ScheduleRunner runner(c,a,entries,3);
  assert(runner.valid());
  c.ready=false; assert(runner.tick()==ScheduleTickResult::ClockUnavailable); assert(a.calls==0);
  c.ready=true; assert(runner.tick()==ScheduleTickResult::Applied); assert(a.last.target_temperature_f==68);
  // Manual change must not cause a second call within the applied event.
  a.last={70,FanMode::On}; assert(runner.tick()==ScheduleTickResult::NoOp); assert(a.calls==1);
  c.value={12,30,100}; a.succeeds=false;
  assert(runner.tick()==ScheduleTickResult::ApplyFailed);
  a.succeeds=true; assert(runner.tick()==ScheduleTickResult::Applied); assert(a.last.target_temperature_f==67);
  c.value={23,0,100}; assert(runner.tick()==ScheduleTickResult::Applied); assert(a.last.fan_mode==FanMode::On);
  c.value={0,0,101}; assert(runner.tick()==ScheduleTickResult::NoOp);
  // Skipped days apply only the current event, not a catch-up queue.
  c.value={14,0,104}; auto before=a.calls;
  assert(runner.tick()==ScheduleTickResult::Applied); assert(a.calls==before+1); assert(a.last.target_temperature_f==67);
  ScheduleRunner restarted(c,a,entries,3); assert(restarted.tick()==ScheduleTickResult::Applied);
  c.value={24,0,104}; assert(restarted.tick()==ScheduleTickResult::InvalidTime);
  // One daily event must run every day, even though its value and index are unchanged.
  const ScheduleEntry once[]={{8,0,{68,FanMode::Auto}}};
  c.value={9,0,200}; ScheduleRunner daily(c,a,once,1);
  assert(daily.tick()==ScheduleTickResult::Applied);
  c.value={7,59,201}; assert(daily.tick()==ScheduleTickResult::NoOp);
  c.value={8,0,201}; assert(daily.tick()==ScheduleTickResult::Applied);
  // Distinct events with identical values still end a manual override.
  const ScheduleEntry same[]={{8,0,{68,FanMode::Auto}},{9,0,{68,FanMode::Auto}}};
  c.value={8,0,200}; ScheduleRunner equal(c,a,same,2); assert(equal.tick()==ScheduleTickResult::Applied);
  c.value={9,0,200}; assert(equal.tick()==ScheduleTickResult::Applied);
  const ScheduleEntry duplicate[]={{8,0,{68,FanMode::Auto}},{8,0,{66,FanMode::On}}};
  const ScheduleEntry unsorted[]={{9,0,{68,FanMode::Auto}},{8,0,{66,FanMode::On}}};
  const ScheduleEntry invalid[]={{8,60,{68,FanMode::Auto}}};
  const ScheduleEntry nan[]={{8,0,{std::numeric_limits<float>::quiet_NaN(),FanMode::Auto}}};
  for (auto pair : {std::pair<const ScheduleEntry*,std::size_t>{nullptr,0}, {duplicate,2}, {unsorted,2}, {invalid,1}, {nan,1}}) {
    ScheduleRunner bad(c,a,pair.first,pair.second);
    assert(!bad.valid()); assert(bad.tick()==ScheduleTickResult::InvalidSchedule);
  }
  TimeSource source; NetworkSynchronizedClock local(source,"UTC0"); LocalTime out{};
  assert(local.read(out)==LocalClock::ReadResult::Unavailable);
  source.ready=true; source.value=1767182400; // Dec 31 2025 noon UTC.
  assert(local.read(out)==LocalClock::ReadResult::Ready); auto day=out.day;
  source.value+=86400; assert(local.read(out)==LocalClock::ReadResult::Ready); assert(out.day==day+1);
  // DST day length must not change the civil-day identifier.
  NetworkSynchronizedClock ny(source,"EST5EDT,M3.2.0,M11.1.0");
  source.value=1772946000; // March 8 2026 00:00 EST.
  assert(ny.read(out)==LocalClock::ReadResult::Ready); day=out.day;
  source.value+=23*3600; assert(ny.read(out)==LocalClock::ReadResult::Ready); assert(out.day==day+1);
}
