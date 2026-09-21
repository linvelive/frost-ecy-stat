#pragma once

#include "clock.hpp"

#include <cstdint>

namespace frost {

// Reports whether the event loop and network prerequisites needed by the
// SNTP backend are ready. It does not establish or imply time trust.
class BootClockPrerequisites {
 public:
  virtual ~BootClockPrerequisites() = default;

  virtual bool ready() const = 0;
};

// Event-fed ESP-IDF startup gate. The future platform wiring reports these
// transitions from the appropriate ESP-IDF setup/event callbacks. This class
// deliberately does not register handlers or start Wi-Fi itself.
class EspIdfBootClockPrerequisites final : public BootClockPrerequisites {
 public:
  void mark_event_loop_ready();
  void mark_network_ready();
  void mark_network_unready();

  bool ready() const override;

 private:
  std::atomic<bool> event_loop_ready_{false};
  std::atomic<bool> network_ready_{false};
};

// Monotonic elapsed time is used for the synchronization timeout. Wall-clock
// time must not be used to decide whether wall-clock time is trustworthy.
class MonotonicClock {
 public:
  virtual ~MonotonicClock() = default;

  virtual std::uint32_t now_ms() const = 0;
};

enum class BootClockTickResult : std::uint8_t {
  WaitingForPrerequisites,
  WaitingForSynchronization,
  Ready,
  SynchronizationTimedOut,
  InitializationFailed,
};

// Coordinates one boot's prerequisite ordering and synchronization timeout.
// A timeout does not revoke the source's ability to become ready later when
// the backend reports successful synchronization.
class BootClockCoordinator final {
 public:
  BootClockCoordinator(
      SntpNetworkTimeSource& source,
      BootClockPrerequisites& prerequisites,
      MonotonicClock& monotonic_clock,
      const char* server,
      std::uint32_t synchronization_timeout_ms);

  BootClockTickResult tick();

 private:
  enum class State : std::uint8_t {
    WaitingForPrerequisites,
    WaitingForSynchronization,
    TimedOut,
    Ready,
    InitializationFailed,
  };

  SntpNetworkTimeSource& source_;
  BootClockPrerequisites& prerequisites_;
  MonotonicClock& monotonic_clock_;
  const char* server_;
  std::uint32_t synchronization_timeout_ms_;
  std::uint32_t synchronization_started_ms_ = 0;
  bool source_initialized_ = false;
  State state_ = State::WaitingForPrerequisites;
};

}  // namespace frost
