#include "clock.hpp"

#include <cstdlib>

namespace frost {

SntpNetworkTimeSource::SntpNetworkTimeSource(
    NetworkTimeSourceBackend& backend)
    : backend_(backend) {}

NetworkTimeSourceInitResult SntpNetworkTimeSource::initialize(
    const char* server) {
  if (initialized_.load(std::memory_order_acquire)) {
    return NetworkTimeSourceInitResult::AlreadyInitialized;
  }
  if (server == nullptr || *server == '\0') {
    return NetworkTimeSourceInitResult::InvalidConfiguration;
  }

  synchronized_.store(false, std::memory_order_release);

  if (backend_.register_sync_handler(&SntpNetworkTimeSource::on_synchronized,
                                     this) != NetworkTimeBackendResult::Ok) {
    return NetworkTimeSourceInitResult::SyncHandlerRegistrationFailed;
  }

  if (backend_.initialize_sntp(server) != NetworkTimeBackendResult::Ok) {
    backend_.unregister_sync_handler();
    return NetworkTimeSourceInitResult::SntpInitializationFailed;
  }

  if (backend_.start_sntp() != NetworkTimeBackendResult::Ok) {
    backend_.deinitialize_sntp();
    backend_.unregister_sync_handler();
    return NetworkTimeSourceInitResult::SntpStartFailed;
  }

  initialized_.store(true, std::memory_order_release);
  return NetworkTimeSourceInitResult::Initialized;
}

bool SntpNetworkTimeSource::is_synchronized() const {
  return initialized_.load(std::memory_order_acquire) &&
         synchronized_.load(std::memory_order_acquire);
}

std::time_t SntpNetworkTimeSource::utc_now() const {
  return backend_.utc_now();
}

void SntpNetworkTimeSource::on_synchronized(void* context) {
  auto* source = static_cast<SntpNetworkTimeSource*>(context);
  if (source != nullptr) {
    source->synchronized_.store(true, std::memory_order_release);
  }
}

NetworkSynchronizedClock::NetworkSynchronizedClock(
    NetworkTimeSource& source,
    const char* posix_timezone)
    : source_(source), posix_timezone_(posix_timezone) {}

LocalClock::ReadResult NetworkSynchronizedClock::read(
    LocalTime& local_time) {
  if (!source_.is_synchronized()) {
    return ReadResult::Unavailable;
  }

  if (!timezone_configured_ && !configure_timezone()) {
    return ReadResult::Unavailable;
  }

  const std::time_t utc_now = source_.utc_now();
  std::tm local_time_parts{};
  if (localtime_r(&utc_now, &local_time_parts) == nullptr) {
    return ReadResult::Unavailable;
  }

  if (local_time_parts.tm_hour < 0 || local_time_parts.tm_hour >= 24 ||
      local_time_parts.tm_min < 0 || local_time_parts.tm_min >= 60) {
    return ReadResult::Unavailable;
  }

  // Gregorian civil-date ordinal, independent of UTC offsets and DST day length.
  int year = local_time_parts.tm_year + 1900;
  const unsigned month = static_cast<unsigned>(local_time_parts.tm_mon + 1);
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned shifted_month = month > 2 ? month - 3 : month + 9;
  const unsigned doy = (153 * shifted_month + 2) / 5 + local_time_parts.tm_mday - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  local_time.day = static_cast<std::int64_t>(era) * 146097 + doe;
  local_time.hour = static_cast<std::uint8_t>(local_time_parts.tm_hour);
  local_time.minute = static_cast<std::uint8_t>(local_time_parts.tm_min);
  return ReadResult::Ready;
}

bool NetworkSynchronizedClock::configure_timezone() {
  if (posix_timezone_ == nullptr || *posix_timezone_ == '\0') {
    return false;
  }

  if (setenv("TZ", posix_timezone_, 1) != 0) {
    return false;
  }
  tzset();
  timezone_configured_ = true;
  return true;
}

}  // namespace frost
