if(CONFIG_ZMK_BEHAVIOR_KEY_LAYER)
  zephyr_library()
  zephyr_library_sources(src/behaviors/behavior_key_layer.c)
endif()
