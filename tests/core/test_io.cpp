
#include "support/Harness.h"
#include "support/Models.h"

#include "blocks/ControlBlocks.h"
#include "blocks/SubsystemBlock.h"
#include "engine/RealTimePacer.h"
#include "engine/Simulator.h"
#include "io/CustomBlock.h"
#include "io/LibrarySerializer.h"
#include "io/ModelSerializer.h"
#include "model/BlockRegistry.h"
#include "scripting/PythonEngine.h"

#include <cmath>
#include <set>
#include <string>
#include <vector>

using namespace simupy;

namespace {

struct Chain {
    Block* source;
    Block* gain;
    Block* sink;
};

Chain buildChain(Model& model, double value, double gain) {
    Chain chain;
    chain.source = model.addBlock("Constant", 0, 0);
    chain.source->params().set("value", std::vector<double>{value});
    chain.source->setSignalName(0, "input");

    chain.gain = model.addBlock("Gain", 200, 0);
    chain.gain->params().set("gain", std::vector<double>{gain});

    chain.sink = model.addBlock("Scope", 400, 0);

    model.connect(chain.source->id(), 0, chain.gain->id(), 0);
    model.connect(chain.gain->id(), 0, chain.sink->id(), 0);
    return chain;
}

}

void testCopyPasteRoundTrip() {
    beginTest("Copied blocks paste back complete");

    Model model;
    const Chain chain = buildChain(model, 3.0, 4.0);

    const std::string clipboard = ModelSerializer::copySelection(
        model, {chain.source->id(), chain.gain->id()});
    check(ModelSerializer::isPastable(clipboard), "the copy is recognisable");

    const std::vector<std::string> created =
        ModelSerializer::pasteInto(model, clipboard, 0.0, 300.0);

    check(created.size() == 2, "both blocks arrive");
    check(model.blocks().size() == 5, "and are added, not substituted");
    if (created.size() != 2) return;

    // Fresh ids, or the paste would have overwritten what it was copied from.
    check(created[0] != chain.source->id() && created[1] != chain.gain->id(),
          "the copies get their own ids");

    const Block* pastedSource = model.block(created[0]);
    const Block* pastedGain = model.block(created[1]);
    check(pastedSource && pastedGain, "both are findable by their new id");
    if (!pastedSource || !pastedGain) return;

    checkClose(pastedSource->params().vector("value").at(0), 3.0, 1e-12,
               "parameters come along");
    checkClose(pastedGain->params().vector("gain").at(0), 4.0, 1e-12,
               "on every block");
    check(pastedSource->signalName(0) == "input", "so do signal names");
    check(pastedSource->name() != chain.source->name(),
          "and the name is made unique rather than duplicated");

    // The wire must join the copies rather than reach back into the originals.
    const Connection* wire = model.incoming(created[1], 0);
    check(wire != nullptr, "the wire between them survives");
    check(wire && wire->sourceBlock == created[0],
          "and connects the copies, not the originals");

    checkClose(model.geometry(created[0]).y - model.geometry(chain.source->id()).y,
               300.0, 1e-9, "the offset is applied");
}

void testCopyDropsHalfWires() {
    beginTest("A partly selected wire is not copied");

    Model model;
    const Chain chain = buildChain(model, 1.0, 2.0);

    // Only the Gain: both of its wires reach outside.
    const std::string clipboard =
        ModelSerializer::copySelection(model, {chain.gain->id()});

    const std::size_t wiresBefore = model.connections().size();
    const std::vector<std::string> created =
        ModelSerializer::pasteInto(model, clipboard, 0.0, 300.0);

    check(created.size() == 1, "just the one block is pasted");
    check(model.connections().size() == wiresBefore,
          "and no wire came with it");
}

void testCopySubsystemTakesContents() {
    beginTest("Copying a subsystem takes what is inside it");

    Model model;
    Block* subsystem = addAffineSubsystem(model, 0, 0, 3.0, 1.5);

    const std::string clipboard =
        ModelSerializer::copySelection(model, {subsystem->id()});
    const std::vector<std::string> created =
        ModelSerializer::pasteInto(model, clipboard, 300.0, 0.0);

    check(created.size() == 1, "the subsystem is pasted");
    if (created.empty()) return;

    const auto* copy =
        dynamic_cast<const SubsystemBlock*>(model.block(created.front()));
    check(copy != nullptr, "and is still a subsystem");
    check(copy && copy->contents().blocks().size() == 5,
          "with all five inner blocks");
    check(copy && copy->contents().connections().size() == 4,
          "and all four inner wires");

    Block* source = model.addBlock("Constant", -200, 0);
    source->params().set("value", std::vector<double>{2.0});
    Block* scope = model.addBlock("Scope", 600, 0);
    model.connect(source->id(), 0, created.front(), 0);
    model.connect(created.front(), 0, scope->id(), 0);
    model.solver().stopTime = 0.2;

    checkClose(runToEnd(model), 3.0 * 2.0 + 1.5, 1e-12,
               "and it computes what the original did");
}

void testPasteTwice() {
    beginTest("Pasting twice makes two independent copies");

    Model model;
    const Chain chain = buildChain(model, 5.0, 1.0);
    const std::string clipboard = ModelSerializer::copySelection(
        model, {chain.source->id(), chain.gain->id()});

    const std::vector<std::string> first =
        ModelSerializer::pasteInto(model, clipboard, 0.0, 300.0);
    const std::vector<std::string> second =
        ModelSerializer::pasteInto(model, clipboard, 0.0, 600.0);

    check(model.blocks().size() == 7, "seven blocks in all");
    check(first[0] != second[0] && first[1] != second[1],
          "the second paste gets its own ids");

    // Names stay unique because the logged signal paths are built from them.
    std::set<std::string> names;
    for (const BlockPtr& block : model.blocks())
        check(names.insert(block->name()).second,
              "every block name is still unique");

    const Connection* wire = model.incoming(second[1], 0);
    check(wire && wire->sourceBlock == second[0],
          "and the second copy is wired to itself");
}

void testPasteRejectsRubbish() {
    beginTest("Pasting something else is refused");

    Model model;
    model.addBlock("Constant", 0, 0);

    check(!ModelSerializer::isPastable("not json at all"),
          "plain text is not pastable");
    check(!ModelSerializer::isPastable(R"({"format":"simupy-model"})"),
          "and neither is a whole model file");

    bool threw = false;
    try {
        ModelSerializer::pasteInto(model, "{\"format\":\"something-else\"}");
    } catch (const ModelError&) {
        threw = true;
    }
    check(threw, "pasting it throws");
    check(model.blocks().size() == 1, "and changes nothing");
}

void runIoTests() {
    runTest(testCopyPasteRoundTrip);
    runTest(testCopyDropsHalfWires);
    runTest(testCopySubsystemTakesContents);
    runTest(testPasteTwice);
    runTest(testPasteRejectsRubbish);
}
