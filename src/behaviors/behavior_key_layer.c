/*
 * SPDX-License-Identifier: MIT
 *
 * zmk-behavior-key-layer
 *
 * Behavior contract:
 *   - trigger press: activate layer and press the configured key
 *   - another physical key press: release configured key only
 *   - trigger release: ensure key is released and deactivate layer
 *
 * NOTE: This module is a starting implementation and must be compiled
 * against the exact ZMK revision used by your firmware. ZMK internal
 * APIs and event structures can vary between revisions.
 */

#define DT_DRV_COMPAT zmk_behavior_key_layer

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct key_layer_config {
    bool release_on_other_key;
};

struct key_layer_state {
    bool active;
    bool simulated_key_down;
    bool layer_active;
    uint32_t trigger_position;
    uint32_t keycode;
    uint8_t layer;
    bool release_on_other_key;
};

/*
 * This implementation intentionally supports one active instance at a
 * time. Use a per-device state structure if multiple independent instances
 * must be held concurrently.
 */
static struct key_layer_state state;

static int release_simulated_key(uint32_t timestamp) {
    if (!state.simulated_key_down) {
        return 0;
    }

    int ret = raise_zmk_keycode_state_changed_from_encoded(state.keycode, false, timestamp);

    if (ret == 0) {
        state.simulated_key_down = false;
    }

    return ret;
}

static int key_layer_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state || !state.active) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Ignore the physical position that initiated this behavior. */
    if (ev->position == state.trigger_position) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Optionally release the simulated key on another physical press. */
    if (!state.release_on_other_key) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Other physical key pressed: release key, leave layer active. */
    int ret = release_simulated_key(ev->timestamp);
    if (ret < 0) {
        LOG_ERR("Failed to release simulated key: %d", ret);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(key_layer, key_layer_position_listener);
ZMK_SUBSCRIPTION(key_layer, zmk_position_state_changed);

static int on_key_layer_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    LOG_INF("key_layer pressed: position=%d param1=%d param2=%d",
        event.position, binding->param1, binding->param2);

    if (state.active) {
        LOG_WRN("key-layer behavior already active; ignoring press");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    const struct key_layer_config *config = binding->behavior_dev->config;
    
    state.active = true;
    state.trigger_position = event.position;
    state.keycode = binding->param1;
    state.layer = binding->param2;
    state.simulated_key_down = false;
    state.layer_active = false;
    state.release_on_other_key = config->release_on_other_key;

    int ret = zmk_keymap_layer_activate(state.layer);
    if (ret < 0) {
        LOG_ERR("Failed to activate layer %u: %d", state.layer, ret);
        state.active = false;
        return ZMK_BEHAVIOR_OPAQUE;
    }
    state.layer_active = true;

    ret = raise_zmk_keycode_state_changed_from_encoded(
        state.keycode, true, event.timestamp);

    if (ret < 0) {
        LOG_ERR("Failed to press simulated key: %d", ret);
        (void)zmk_keymap_layer_deactivate(state.layer);
        state.layer_active = false;
        state.active = false;
        return ZMK_BEHAVIOR_OPAQUE;
    }

    state.simulated_key_down = true;
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_key_layer_released(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    ARG_UNUSED(binding);

    if (!state.active || state.trigger_position != event.position) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    int key_ret = release_simulated_key(event.timestamp);

    int layer_ret = 0;
    if (state.layer_active) {
        layer_ret = zmk_keymap_layer_deactivate(state.layer);
    }

    state.layer_active = false;
    state.active = false;

    if (key_ret < 0) {
        LOG_ERR("Failed to release simulated key: %d", key_ret);
    }
    if (layer_ret < 0) {
        LOG_ERR("Failed to deactivate layer: %d", layer_ret);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

#include <zephyr/sys/util.h>

static const struct behavior_parameter_value_metadata param1_values[] = {
    {
        .display_name = "Key",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_HID_USAGE,
    },
};

static const struct behavior_parameter_value_metadata param2_values[] = {
    {
        .display_name = "Layer",
        .type = BEHAVIOR_PARAMETER_VALUE_TYPE_LAYER_ID,
    },
};

static const struct behavior_parameter_metadata_set param_metadata_sets[] = {
    {
        .param1_values = param1_values,
        .param1_values_len = ARRAY_SIZE(param1_values),
        .param2_values = param2_values,
        .param2_values_len = ARRAY_SIZE(param2_values),
    },
};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_sets),
    .sets = param_metadata_sets,
};

#endif

static const struct behavior_driver_api key_layer_driver_api = {
    .binding_pressed = on_key_layer_pressed,
    .binding_released = on_key_layer_released,

    #if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
    #endif
};

#define KEY_LAYER_INST(n) \
    static const struct key_layer_config key_layer_config_##n = { \
            .release_on_other_key = DT_INST_PROP(n, release_on_other_key), \
        }; \
    BEHAVIOR_DT_INST_DEFINE( \
        n, \
        NULL, \
        NULL, \
        NULL, \
        &key_layer_config_##n, \
        POST_KERNEL, \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, \
        &key_layer_driver_api \
    );

DT_INST_FOREACH_STATUS_OKAY(KEY_LAYER_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
