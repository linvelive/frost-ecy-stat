#pragma once

#include "schedule.hpp"

#include <atomic>
#include <ctime>

namespace frost {

// The platform adapter owns the synchronization signal. A non-zero system
// clock is not sufficient evidence that wall time is trustworthy after boot.
class NetworkTimeSource {
 public:
  virtual ~NetworkTimeSource() = default;

  virtual bool is_synchronized() const = 0;
  virtual std::time_t utc_now() const = 0;
};

enum class NetworkTimeBackendResult : std::uint8_t {
  Ok,
  Error,
};

class NetworkTimeSourceBackend {
 public:
  using SyncCallback = void (*)(void* context);

  virtual ~NetworkTimeSourceBackend() = default;

  virtual NetworkTimeBackendResult register_sync_handler(
      SyncCallback callback,
      void* context) = 0;
  virtual NetworkTimeBackendResult initialize_sntp(
      const char* server) = 0;
  virtual NetworkTimeBackendResult start_sntp() = 0;
  virtual void unregister_sync_handler() = 0;
  virtual void deinitialize_sntp() = 0;
  virtual std::time_t utc_now() const = 0;
};

enum class NetworkTimeSourceInitResult : std::uint8_t {
  Initialized,
  AlreadyInitialized,
  InvalidConfiguration,
  SyncHandlerRegistrationFailed,
  SntpInitializationFailed,
  SntpStartFailed,
};

// Owns the trust transition from an ESP-IDF SNTP backend to the platform-
// neutral NetworkTimeSource contract. A system clock value is not trusted
// until the backend invokes the synchronization callback.
class SntpNetworkTimeSource final : public NetworkTimeSource {
 public:
  explicit SntpNetworkTimeSource(NetworkTimeSourceBackend& backend);

  NetworkTimeSourceInitResult initialize(const char* server);

  bool is_synchronized() const override;
  std::time_t utc_now() const override;

 private:
  static void on_synchronized(void* context);

  NetworkTimeSourceBackend& backend_;
  std::atomic<bool> initialized_{false};
  std::atomic<bool> synchronized_{false};
};

// Converts trusted network-synchronized UTC into configured local wall time.
// The POSIX TZ string supplies the timezone and DST rules used by localtime_r.
class NetworkSynchronizedClock final : public LocalClock {
 public:
  NetworkSynchronizedClock(
      NetworkTimeSource& source,
      const char* posix_timezone);

  ReadResult read(LocalTime& local_time) override;

 private:
  bool configure_timezone();

  NetworkTimeSource& source_;
  const char* posix_timezone_;
  bool timezone_configured_ = false;
};

}  // namespace frost
