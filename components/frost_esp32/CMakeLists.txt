idf_component_register(SRCS "src/ecy_stat_ble_transport.cpp" "src/esp32_session.cpp"
                       INCLUDE_DIRS "include" REQUIRES frost bt nvs_flash)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
