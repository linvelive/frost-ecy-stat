idf_component_register(SRCS "src/ecy_stat_codec.cpp" "src/ecy_stat_controller.cpp"
                       INCLUDE_DIRS "include")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
