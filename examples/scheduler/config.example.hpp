#pragma once
// Copy OUTSIDE this repository. Never publish configured binaries.
#define FROST_WIFI_SSID "your-network"
#define FROST_WIFI_PASSWORD "your-password"
#define FROST_POSIX_TIMEZONE "EST5EDT,M3.2.0,M11.1.0"
#define FROST_SNTP_SERVER "pool.ntp.org"
// Sorted local times, repeated every day. Fahrenheit wire values only.
inline constexpr frost::ScheduleEntry kSchedule[] = {
  {8, 0, {68.0f, frost::FanMode::Auto}},
  {23, 0, {66.0f, frost::FanMode::On}},
};
