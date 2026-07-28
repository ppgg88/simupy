#include "SubsystemBlock.h"

#include "BlockUtils.h"

#include <algorithm>

namespace simupy {
namespace {

class InportBlock : public Block {
public:
    PortLayout ports() const override { return {{}, {"out"}}; }

    void setup(BlockSetup& s) override {
        const int width = std::max(1, params().integer("width", 1));
        s.outputWidths[0] = width;
    }

    void computeOutputs(const EvalContext& c) override { c.out(0).setZero(); }
};

class OutportBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {}}; }

    void setup(BlockSetup& s) override {
        s.outputWidths.clear();
        s.directFeedthrough.assign(s.inputCount(), true);
    }

    void computeOutputs(const EvalContext& c) override { (void)c; }

    bool logsInputs() const override { return true; }
};

}  // namespace

bool isSubsystem(const Block* block) {
    return dynamic_cast<const SubsystemBlock*>(block) != nullptr;
}

bool isInport(const Block* block) {
    return block && block->typeName() == "Inport";
}

bool isOutport(const Block* block) {
    return block && block->typeName() == "Outport";
}

int portNumberOf(const Block* block) {
    return block ? block->params().integer("portNumber", 1) : 1;
}

SubsystemBlock::SubsystemBlock() : contents_(std::make_unique<Model>()) {}

SubsystemBlock::~SubsystemBlock() = default;

std::vector<Block*> SubsystemBlock::portBlocks(bool inputs) const {
    std::vector<Block*> found;
    for (const BlockPtr& block : contents_->blocks()) {
        const bool matches =
            inputs ? isInport(block.get()) : isOutport(block.get());
        if (matches) found.push_back(block.get());
    }

    std::sort(found.begin(), found.end(), [](const Block* a, const Block* b) {
        return portNumberOf(a) < portNumberOf(b);
    });

    std::vector<Block*> ordered;
    for (Block* block : found) {
        const int slot = std::max(1, portNumberOf(block));
        if (static_cast<int>(ordered.size()) < slot) ordered.resize(slot, nullptr);
        ordered[slot - 1] = block;
    }
    return ordered;
}

PortLayout SubsystemBlock::ports() const {
    PortLayout layout;
    for (Block* block : portBlocks(true))
        layout.inputs.push_back(block ? block->name() : "in");
    for (Block* block : portBlocks(false))
        layout.outputs.push_back(block ? block->name() : "out");
    return layout;
}

void SubsystemBlock::setup(BlockSetup& s) {
    // Reached only if something bypassed flattening; failing loudly beats
    // silently simulating an empty box.
    (void)s;
    throw ModelError(
        "a subsystem cannot be simulated directly — it should have been "
        "expanded before compilation");
}

void SubsystemBlock::computeOutputs(const EvalContext& c) { (void)c; }

void registerSubsystemBlocks() {
    registerBlockType<InportBlock>(
        "Inport", "Ports", "Brings a signal into the enclosing subsystem.",
        {intParam("portNumber", "Port number", 1, 1, 256),
         intParam("width", "Width when used at the top level", 1)},
        70.0, 45.0);

    registerBlockType<OutportBlock>(
        "Outport", "Ports", "Sends a signal out of the enclosing subsystem.",
        {intParam("portNumber", "Port number", 1, 1, 256)}, 70.0, 45.0);

    registerBlockType<SubsystemBlock>(
        "Subsystem", "Ports",
        "A diagram inside a block. Double-click to open it.", {}, 110.0, 80.0);
}

}  // namespace simupy
