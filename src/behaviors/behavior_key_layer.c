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

#include <string.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define KEY_LAYER_MAX_ACTIVE 5

struct key_layer_config {
    bool release_on_other_key;
    bool toggle_mode;
};

struct key_layer_state {
    uint32_t position;
    uint32_t key;
    uint8_t layer;
    bool is_key_down;
    bool is_layer_active;
    bool release_on_other_key;
};

static struct key_layer_state key_layer_states[KEY_LAYER_MAX_ACTIVE];

static struct key_layer_state* find_state_by_position(uint32_t position) {
    for (int i = 0; i < KEY_LAYER_MAX_ACTIVE; i++) {
        if (key_layer_states[i].position == position) {
            return &key_layer_states[i];
        }
    }

    LOG_DBG("find_state_by_position: not found");

    return NULL;
}

static struct key_layer_state* get_empty_key_layer_state() {
    for (int i = 0; i < KEY_LAYER_MAX_ACTIVE; i++) {
        if (!key_layer_states[i].position) {
            return &key_layer_states[i];
        }
    }

    LOG_DBG("get_empty_key_layer_state: not found");

    return NULL;
}

static int release_simulated_key(struct key_layer_state *state, uint32_t timestamp) {
    if (!state->is_key_down) {
        return 0;
    }

    int ret = raise_zmk_keycode_state_changed_from_encoded(state->key, false, timestamp);

    if (ret == 0) {
        state->is_key_down = false;
    }else{
        LOG_ERR("Failed to release key: %d", ret);
    }

    return ret;
}

static int key_layer_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);

    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < KEY_LAYER_MAX_ACTIVE; i++) {
        struct key_layer_state *state = &key_layer_states[i];

        if (!state->position) {
            continue;
        }

        /* 忽略当前实例自己的触发按键 */
        if (ev->position == state->position) {
            continue;
        }

        if (!state->release_on_other_key) {
            continue;
        }

        release_simulated_key(state, ev->timestamp);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(key_layer, key_layer_position_listener);
ZMK_SUBSCRIPTION(key_layer, zmk_position_state_changed);

static int deactivate_key_layer(struct key_layer_state *state, uint32_t timestamp) {
    int key_ret = release_simulated_key(state, timestamp);

    int layer_ret = 0;
    if (state->is_layer_active) {
        layer_ret = zmk_keymap_layer_deactivate(state->layer);
    }

    if (layer_ret < 0) {
        LOG_ERR("Failed to deactivate layer: %d", layer_ret);
    }

    memset(state, 0, sizeof(state));

    return (key_ret < 0) ? key_ret : layer_ret;
}

static int on_key_layer_pressed(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct key_layer_config *config = dev->config;

    struct key_layer_state *state = find_state_by_position(event.position);

    LOG_DBG("on_key_layer_pressed - state: %p", state);

    if (state) {
        if (config->toggle_mode) {
            (void)deactivate_key_layer(state, event.timestamp);
        } else {
            LOG_WRN("key-layer behavior already active; ignoring press");
        }
        return ZMK_BEHAVIOR_OPAQUE;
    }

    state = get_empty_key_layer_state();

    if (!state) {
        LOG_ERR("Failed to get_empty_key_layer_state");
        return ZMK_BEHAVIOR_OPAQUE;
    }
    
    state->position = event.position;
    state->key = binding->param1;
    state->layer = binding->param2;
    state->is_key_down = false;
    state->is_layer_active = false;
    state->release_on_other_key = config->release_on_other_key;

    int ret = zmk_keymap_layer_activate(state->layer);
    if (ret < 0) {
        LOG_ERR("Failed to activate layer %u: %d", state->layer, ret);
        return ZMK_BEHAVIOR_OPAQUE;
    }
    state->is_layer_active = true;

    ret = raise_zmk_keycode_state_changed_from_encoded(state->key, true, event.timestamp);
    if (ret < 0) {
        LOG_ERR("Failed to press simulated key: %d", ret);
        (void)zmk_keymap_layer_deactivate(state->layer);
        return ZMK_BEHAVIOR_OPAQUE;
    }
    state->is_key_down = true;

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_key_layer_released(
    struct zmk_behavior_binding *binding,
    struct zmk_behavior_binding_event event) {

    struct key_layer_state *state = find_state_by_position(event.position);

    if (!state) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct key_layer_config *config = dev->config;

    if (config->toggle_mode) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    int key_ret = release_simulated_key(state, event.timestamp);

    int layer_ret = 0;
    if (state->is_layer_active) {
        layer_ret = zmk_keymap_layer_deactivate(state->layer);
    }

    if (key_ret < 0) {
        LOG_ERR("Failed to release simulated key: %d", key_ret);
    }
    if (layer_ret < 0) {
        LOG_ERR("Failed to deactivate layer: %d", layer_ret);
    }

    memset(state, 0, sizeof(state));

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
            .release_on_other_key = DT_INST_PROP_OR(n, release_on_other_key, false), \
            .toggle_mode = DT_INST_PROP_OR(n, toggle_mode, false), \
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
