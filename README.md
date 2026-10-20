# zmk-behavior-key-layer

A ZMK out-of-tree behavior that combines a simulated held key with a temporary layer.

## Behavior

- Trigger press: activate the configured layer and press the configured key.
- While held, another physical key press: release the configured key immediately, but keep the layer active.
- Trigger release: release the configured key if still held and deactivate the layer.

## Devicetree usage

Add a behavior node:

```dts
/ {
    behaviors {
        key_layer: key_layer {
            compatible = "zmk,behavior-key-layer";
            #binding-cells = <2>;
        };
    };
};
```

Use it in a keymap, for example:

```dts
&key_layer LCTRL 3
```

The first parameter is the keycode to simulate; the second is the target layer index (zero-based).

## Integrate with ZMK

Add this repository to your West manifest or make it available through `ZMK_EXTRA_MODULES`, according to your build setup. Ensure the module path is visible to Zephyr's module discovery.

## Important limitations

- This first version uses a single global state and therefore supports only one active instance at a time.
- This source has not been compiled against your specific ZMK revision. ZMK's internal APIs and event structures can change; verify it with your exact checkout before flashing.
- Test on a spare firmware build first, especially key release behavior and layer activation/deactivation.
