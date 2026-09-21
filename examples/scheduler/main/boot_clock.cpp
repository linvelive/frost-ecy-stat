#include "boot_clock.hpp"

namespace frost {

BootClockCoordinator::BootClockCoordinator(
    SntpNetworkTimeSource& source,
    BootClockPrerequisites& prerequisites,
    MonotonicClock& monotonic_clock,
    const char* server,
    std::uint32_t synchronization_timeout_ms)
    : source_(source),
      prerequisites_(prerequisites),
      monotonic_clock_(monotonic_clock),
      server_(server),
      synchronization_timeout_ms_(synchronization_timeout_ms) {}

void EspIdfBootClockPrerequisites::mark_event_loop_ready() {
  event_loop_ready_.store(true, std::memory_order_release);
}

void EspIdfBootClockPrerequisites::mark_network_ready() {
  network_ready_.store(true, std::memory_order_release);
}

void EspIdfBootClockPrerequisites::mark_network_unready() {
  network_ready_.store(false, std::memory_order_release);
}

bool EspIdfBootClockPrerequisites::ready() const {
  return event_loop_ready_.load(std::memory_order_acquire) &&
         network_ready_.load(std::memory_order_acquire);
}

BootClockTickResult BootClockCoordinator::tick() {
  if (state_ == State::Ready) {
    return BootClockTickResult::Ready;
  }
  if (state_ == State::InitializationFailed) {
    return BootClockTickResult::InitializationFailed;
  }

  if (!source_initialized_) {
    if (!prerequisites_.ready()) {
      return BootClockTickResult::WaitingForPrerequisites;
    }

    if (source_.initialize(server_) !=
        NetworkTimeSourceInitResult::Initialized) {
      state_ = State::InitializationFailed;
      return BootClockTickResult::InitializationFailed;
    }

    source_initialized_ = true;
    synchronization_started_ms_ = monotonic_clock_.now_ms();
    state_ = State::WaitingForSynchronization;
  }

  if (source_.is_synchronized()) {
    state_ = State::Ready;
    return BootClockTickResult::Ready;
  }

  const std::uint32_t elapsed_ms =
      monotonic_clock_.now_ms() - synchronization_started_ms_;
  if (elapsed_ms >= synchronization_timeout_ms_) {
    state_ = State::TimedOut;
    return BootClockTickResult::SynchronizationTimedOut;
  }

  state_ = State::WaitingForSynchronization;
  return BootClockTickResult::WaitingForSynchronization;
}

}  // namespace frost
