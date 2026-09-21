idf_component_register(SRCS "main.cpp" "production_boot.cpp" "schedule.cpp" "clock.cpp"
  "boot_clock.cpp" "esp_boot_clock_event_bridge.cpp" "esp_sntp_backend.cpp"
  INCLUDE_DIRS "." REQUIRES frost_esp32 esp_event esp_netif esp_wifi esp_timer)
set(FROST_RUNTIME_CONFIG_HEADER "" CACHE FILEPATH "External private scheduler configuration")
if(FROST_RUNTIME_CONFIG_HEADER)
  if(NOT IS_ABSOLUTE "${FROST_RUNTIME_CONFIG_HEADER}" OR NOT EXISTS "${FROST_RUNTIME_CONFIG_HEADER}")
    message(FATAL_ERROR "FROST_RUNTIME_CONFIG_HEADER must be an existing absolute path")
  endif()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE FROST_RUNTIME_CONFIG_HEADER="${FROST_RUNTIME_CONFIG_HEADER}")
endif()
