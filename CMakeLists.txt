# 使用 zephyr_library_amend() 将本模块的源码添加到 Zephyr 库中
zephyr_library_amend()

# 现在，zephyr_library_sources 和 zephyr_library_include_directories 
# 会作用于被“修补”的那个库
zephyr_library_sources(
    src/behaviors/behavior_key_layer.c
)

message(STATUS ">>> ZMK_BEHAVIOR_KEY_LAYER = ${CONFIG_ZMK_BEHAVIOR_KEY_LAYER}")

if(CONFIG_ZMK_BEHAVIOR_KEY_LAYER)
  message(STATUS ">>> Compiling behavior_key_layer.c")
  zephyr_library()
  zephyr_library_sources(src/behaviors/behavior_key_layer.c)
else()
  message(WARNING ">>> ZMK_BEHAVIOR_KEY_LAYER is NOT enabled")
endif()