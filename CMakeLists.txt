target_include_directories(app PRIVATE
    ${ZMK_APP_DIR}/include
)

message(STATUS ">>> ZMK_BEHAVIOR_KEY_LAYER = ${CONFIG_ZMK_BEHAVIOR_KEY_LAYER}")

if(CONFIG_ZMK_BEHAVIOR_KEY_LAYER)
  message(STATUS ">>> Compiling behavior_key_layer.c")
  zephyr_library()
  zephyr_library_sources(src/behaviors/behavior_key_layer.c)
else()
  message(WARNING ">>> ZMK_BEHAVIOR_KEY_LAYER is NOT enabled")
endif()