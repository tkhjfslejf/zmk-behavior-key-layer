if(CONFIG_ZMK_BEHAVIOR_KEY_LAYER)
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_KEY_LAYER app PRIVATE src/behaviors/behavior_key_layer.c)
endif()