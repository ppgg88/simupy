#include "ControlBlocks.h"

#include "BlockUtils.h"

#include <algorithm>

namespace simupy {
namespace {

class SliderBlock : public InteractiveBlock {
public:
    ControlDescriptor descriptor() const override {
        ControlDescriptor d;
        d.kind = ControlDescriptor::Kind::Slider;
        d.label = params().text("label", "");
        d.minimum = params().real("minimum", 0.0);
        d.maximum = params().real("maximum", 1.0);
        d.step = params().real("step", 0.0);
        // A reversed range is a typo, not an intention, and silently
        // swallowing it would give a control that cannot move.
        if (d.maximum <= d.minimum) d.maximum = d.minimum + 1.0;
        return d;
    }

    double initialValue() const override {
        const ControlDescriptor d = descriptor();
        return std::clamp(params().real("value", 0.0), d.minimum, d.maximum);
    }

    void commitValue(double value) override { params().set("value", value); }
};

class ToggleBlock : public InteractiveBlock {
public:
    ControlDescriptor descriptor() const override {
        ControlDescriptor d;
        d.kind = ControlDescriptor::Kind::Toggle;
        d.label = params().text("label", "");
        d.offValue = params().real("offValue", 0.0);
        d.onValue = params().real("onValue", 1.0);
        return d;
    }

    double initialValue() const override {
        const ControlDescriptor d = descriptor();
        return params().boolean("on", false) ? d.onValue : d.offValue;
    }

    void commitValue(double value) override {
        const ControlDescriptor d = descriptor();
        params().set("on", value == d.onValue);
    }
};

class PushButtonBlock : public InteractiveBlock {
public:
    ControlDescriptor descriptor() const override {
        ControlDescriptor d;
        d.kind = ControlDescriptor::Kind::Button;
        d.label = params().text("label", "");
        d.offValue = params().real("offValue", 0.0);
        d.onValue = params().real("onValue", 1.0);
        return d;
    }

    double initialValue() const override {
        return params().real("offValue", 0.0);
    }

    void commitValue(double) override {}
};

}  // namespace

bool isInteractive(const Block* block) {
    return dynamic_cast<const InteractiveBlock*>(block) != nullptr;
}

void registerControlBlocks() {
    registerBlockType<SliderBlock>(
        "Slider", "Controls",
        "A value you drag while the model runs. Appears in the Controls "
        "panel; changes take effect at the next solver step.",
        {realParam("value", "Value", 0.0),
         realParam("minimum", "Minimum", 0.0),
         realParam("maximum", "Maximum", 1.0),
         realParam("step", "Step", 0.0,
                   "Granularity of the slider. 0 picks one from the range."),
         textParam("label", "Label", "",
                   "Shown in the Controls panel. Defaults to the block name.")},
        90.0, 55.0);

    registerBlockType<ToggleBlock>(
        "Toggle", "Controls",
        "A two-position switch you flip while the model runs.",
        {boolParam("on", "On", false),
         realParam("offValue", "Off value", 0.0),
         realParam("onValue", "On value", 1.0),
         textParam("label", "Label", "")},
        90.0, 55.0);

    registerBlockType<PushButtonBlock>(
        "PushButton", "Controls",
        "Outputs its on value only while the button is held down — for "
        "injecting a disturbance or pulsing a reset.",
        {realParam("offValue", "Released value", 0.0),
         realParam("onValue", "Pressed value", 1.0),
         textParam("label", "Label", "")},
        90.0, 55.0);
}

}  // namespace simupy
