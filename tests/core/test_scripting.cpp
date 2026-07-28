
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

ParamSpec realParamSpec(std::string name, double value) {
    ParamSpec spec;
    spec.name = std::move(name);
    spec.label = spec.name;
    spec.kind = ParamSpec::Kind::Real;
    spec.defaultValue = value;
    return spec;
}

CustomBlockDef makeAffineDefinition() {
    Model scratch;
    Block* holder = addAffineSubsystem(scratch, 0, 0, 1.0, 0.0);
    Model& inner = dynamic_cast<SubsystemBlock*>(holder)->contents();

    for (const BlockPtr& block : inner.blocks()) {
        if (block->typeName() == "Gain")
            block->setParamExpression("gain", "[k]");
        else if (block->typeName() == "Constant")
            block->setParamExpression("value", "[bias + 0.5]");
    }

    CustomBlockDef def;
    def.name = "TestAffine";
    def.displayName = "Affine";
    def.category = "Test";
    def.description = "k * u + bias + 0.5";
    def.params = {realParamSpec("k", 1.0), realParamSpec("bias", 0.0)};
    def.icon.kind = BlockIcon::Kind::Text;
    def.icon.data = "ku+b";

    captureBlockDefinition(*holder, def);
    return def;
}

}

void testCustomBlockMask() {
    beginTest("A masked custom block resolves its parameters");

    registerCustomBlock(makeAffineDefinition());

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{4.0});
    Block* affine = model.addBlock("TestAffine", 200, 0);
    Block* scope = model.addBlock("Scope", 400, 0);
    model.connect(source->id(), 0, affine->id(), 0);
    model.connect(affine->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.5;

    check(affine != nullptr && affine->typeName() == "TestAffine",
          "the block keeps its custom type name");
    checkClose(affine->params().real("k"), 1.0, 1e-12,
               "the mask default is applied on creation");

    affine->params().set("k", 3.0);
    affine->params().set("bias", 1.0);
    checkClose(runToEnd(model), 3.0 * 4.0 + 1.5, 1e-12,
               "k and bias reach the inner Gain and Constant");

    // Changing the mask alone must change the result: nothing is baked in.
    affine->params().set("k", -2.0);
    checkClose(runToEnd(model), -2.0 * 4.0 + 1.5, 1e-12,
               "and re-running picks up a new value");
}

void testCustomBlockInstancesAreIndependent() {
    beginTest("Two instances of a custom block are independent");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{2.0});
    Block* first = model.addBlock("TestAffine", 200, 0);
    Block* second = model.addBlock("TestAffine", 200, 200);
    Block* scope = model.addBlock("Scope", 400, 0);

    first->params().set("k", 10.0);
    second->params().set("k", 100.0);
    model.connect(source->id(), 0, first->id(), 0);
    model.connect(first->id(), 0, second->id(), 0);
    model.connect(second->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.5;

    // (10*2 + 0.5) * 100 + 0.5
    checkClose(runToEnd(model), (10.0 * 2.0 + 0.5) * 100.0 + 0.5, 1e-12,
               "each copy uses its own mask values");

    const auto* firstInner = dynamic_cast<const SubsystemBlock*>(first);
    const auto* secondInner = dynamic_cast<const SubsystemBlock*>(second);
    check(firstInner && secondInner &&
              &firstInner->contents() != &secondInner->contents(),
          "and they own separate diagrams");
}

void testLibraryRoundTrip() {
    beginTest("A library round-trips through .spylib");

    CustomLibrary library;
    library.name = "Test Toolbox";
    library.author = "the test suite";
    library.blocks.push_back(makeAffineDefinition());

    CustomLibrary restored;
    LibrarySerializer::fromJson(LibrarySerializer::toJson(library), restored);

    check(restored.name == "Test Toolbox", "the library name survives");
    check(restored.blocks.size() == 1, "its one block survives");
    if (restored.blocks.empty()) return;

    const CustomBlockDef& def = restored.blocks.front();
    check(def.name == "TestAffine", "the type name survives");
    check(def.params.size() == 2, "both mask parameters survive");
    check(def.icon.kind == BlockIcon::Kind::Text && def.icon.data == "ku+b",
          "the icon survives");
    check(def.contents && def.contents->blocks().size() == 5,
          "the saved diagram survives");

    // Losing the expressions would make a restored block ignore its parameters.
    bool foundExpression = false;
    if (def.contents)
        for (const BlockPtr& block : def.contents->blocks())
            if (block->paramExpression("gain") == "[k]") foundExpression = true;
    check(foundExpression, "and so do the mask expressions");
}

void testModelSurvivesMissingLibrary() {
    beginTest("A model opens without the library it was built with");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{4.0});
    Block* affine = model.addBlock("TestAffine", 200, 0);
    affine->params().set("k", 3.0);
    affine->params().set("bias", 1.0);
    Block* scope = model.addBlock("Scope", 400, 0);
    model.connect(source->id(), 0, affine->id(), 0);
    model.connect(affine->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.5;

    const std::string saved = ModelSerializer::toJson(model);

    // Uninstall the library, as if the file had been mailed elsewhere.
    BlockRegistry::instance().unregisterType("TestAffine");
    check(BlockRegistry::instance().find("TestAffine") == nullptr,
          "the type is gone from the registry");

    Model restored;
    LoadReport report;
    ModelSerializer::fromJson(saved, restored, &report);

    check(report.missingTypes.size() == 1 &&
              report.missingTypes.front() == "TestAffine",
          "the load reports what was missing");

    const Block* substituted = restored.block(affine->id());
    check(substituted != nullptr, "the block is still there");
    if (!substituted) return;
    check(substituted->typeName() == "TestAffine",
          "and remembers the type it wants, so installing the library "
          "reconnects it");
    check(dynamic_cast<const SubsystemBlock*>(substituted) != nullptr,
          "standing in as a plain subsystem");

    checkClose(runToEnd(restored), 3.0 * 4.0 + 1.5, 1e-12,
               "and it still computes exactly the same thing");

    // Put it back for whatever runs next.
    registerCustomBlock(makeAffineDefinition());
}

void testSignalNameReachesTheLog() {
    beginTest("A named signal labels its log channel");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{2.5});
    source->setSignalName(0, "reference");
    Block* scope = model.addBlock("Scope", 300, 0);
    model.connect(source->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.2;

    Simulator* simulator = nullptr;
    runToEnd(model, &simulator);
    if (!simulator) return;

    const std::vector<LogChannel>& channels = simulator->log().channels();
    check(channels.size() == 1, "one channel was logged");
    if (channels.empty()) return;
    check(channels.front().label(0) == "reference",
          "and it is labelled with the signal name");

    // Clearing must fall back to the default, not leave an empty legend entry.
    source->setSignalName(0, "");
    runToEnd(model, &simulator);
    check(simulator->log().channels().front().label(0) == "Scope : in",
          "clearing the name falls back to the sink's own label");
}

void testSignalNameRoundTrip() {
    beginTest("Signal names round-trip through JSON");

    Model original;
    Block* source = original.addBlock("Constant", 0, 0);
    source->setSignalName(0, "reference");
    Block* scope = original.addBlock("Scope", 300, 0);
    original.connect(source->id(), 0, scope->id(), 0);

    Model restored;
    ModelSerializer::fromJson(ModelSerializer::toJson(original), restored);

    const Block* back = restored.block(source->id());
    check(back != nullptr, "the block is found by id");
    check(back && back->signalName(0) == "reference", "and kept its label");
}

void testWirelessLinkMatchesAWire() {
    beginTest("Goto/From carries a signal without a wire");

    Model model;
    Block* source = model.addBlock("Constant", 0, 0);
    source->params().set("value", std::vector<double>{3.0});
    Block* send = model.addBlock("Goto", 150, 0);
    send->params().set("tag", std::string("u"));

    Block* receive = model.addBlock("From", 0, 200);
    receive->params().set("tag", std::string("u"));
    Block* gain = model.addBlock("Gain", 150, 200);
    gain->params().set("gain", std::vector<double>{4.0});
    Block* scope = model.addBlock("Scope", 300, 200);

    model.connect(source->id(), 0, send->id(), 0);
    model.connect(receive->id(), 0, gain->id(), 0);
    model.connect(gain->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.2;

    checkClose(runToEnd(model), 12.0, 1e-12, "the value arrives through the tag");

    // Splice points, not blocks to execute.
    Simulator* simulator = nullptr;
    runToEnd(model, &simulator);
    if (!simulator) return;
    bool foundMarker = false;
    for (const CompiledBlock& cb : simulator->compiled().blocks)
        if (cb.block->typeName() == "Goto" || cb.block->typeName() == "From")
            foundMarker = true;
    check(!foundMarker, "and the markers are spliced away");
}

void testWirelessLinkScoping() {
    beginTest("Wireless tags respect their scope");

    {
        Model model;
        Block* source = model.addBlock("Constant", 0, 0);
        source->params().set("value", std::vector<double>{5.0});
        Block* send = model.addBlock("Goto", 150, 0);
        send->params().set("tag", std::string("shared"));
        send->params().set("scope", std::string("global"));
        model.connect(source->id(), 0, send->id(), 0);

        Block* holder = model.addBlock("Subsystem", 0, 200);
        Model& inner = dynamic_cast<SubsystemBlock*>(holder)->contents();
        Block* receive = inner.addBlock("From", 0, 0);
        receive->params().set("tag", std::string("shared"));
        Block* out = inner.addBlock("Outport", 200, 0);
        out->params().set("portNumber", 1.0);
        inner.connect(receive->id(), 0, out->id(), 0);

        Block* scope = model.addBlock("Scope", 300, 200);
        model.connect(holder->id(), 0, scope->id(), 0);
        model.solver().stopTime = 0.2;

        checkClose(runToEnd(model), 5.0, 1e-12,
                   "a global tag crosses a subsystem boundary");
    }

    // A local tag is not visible from the subsystem's own diagram.
    {
        Model model;
        Block* source = model.addBlock("Constant", 0, 0);
        Block* send = model.addBlock("Goto", 150, 0);
        send->params().set("tag", std::string("private"));
        model.connect(source->id(), 0, send->id(), 0);

        Block* holder = model.addBlock("Subsystem", 0, 200);
        Model& inner = dynamic_cast<SubsystemBlock*>(holder)->contents();
        Block* receive = inner.addBlock("From", 0, 0);
        receive->params().set("tag", std::string("private"));
        Block* out = inner.addBlock("Outport", 200, 0);
        out->params().set("portNumber", 1.0);
        inner.connect(receive->id(), 0, out->id(), 0);

        Block* scope = model.addBlock("Scope", 300, 200);
        model.connect(holder->id(), 0, scope->id(), 0);

        std::string message;
        try {
            Simulator simulator(model, pythonExpressionEvaluator());
            simulator.initialize();
        } catch (const ModelError& error) {
            message = error.what();
        }
        check(message.find("private") != std::string::npos,
              "a local tag is not visible from inside a subsystem");
    }
}

void testWirelessLinkDiagnostics() {
    beginTest("Ambiguous and missing tags are reported");

    {
        Model model;
        Block* a = model.addBlock("Constant", 0, 0);
        Block* b = model.addBlock("Constant", 0, 100);
        Block* first = model.addBlock("Goto", 150, 0);
        first->params().set("tag", std::string("x"));
        Block* second = model.addBlock("Goto", 150, 100);
        second->params().set("tag", std::string("x"));
        model.connect(a->id(), 0, first->id(), 0);
        model.connect(b->id(), 0, second->id(), 0);

        Block* receive = model.addBlock("From", 0, 200);
        receive->params().set("tag", std::string("x"));
        Block* scope = model.addBlock("Scope", 200, 200);
        model.connect(receive->id(), 0, scope->id(), 0);

        std::string message;
        try {
            Simulator simulator(model, pythonExpressionEvaluator());
            simulator.initialize();
        } catch (const ModelError& error) {
            message = error.what();
        }
        check(message.find("both Goto blocks tagged 'x'") != std::string::npos,
              "a duplicated tag names both offenders");
    }

    {
        Model model;
        Block* receive = model.addBlock("From", 0, 0);
        receive->params().set("tag", std::string("missing"));
        Block* scope = model.addBlock("Scope", 200, 0);
        model.connect(receive->id(), 0, scope->id(), 0);

        std::string message;
        try {
            Simulator simulator(model, pythonExpressionEvaluator());
            simulator.initialize();
        } catch (const ModelError& error) {
            message = error.what();
        }
        check(message.find("'missing'") != std::string::npos,
              "an unmatched tag is named in the error");
    }
}

void runScriptingTests() {
    runTest(testCustomBlockMask);
    runTest(testCustomBlockInstancesAreIndependent);
    runTest(testLibraryRoundTrip);
    runTest(testModelSurvivesMissingLibrary);
    runTest(testSignalNameReachesTheLog);
    runTest(testSignalNameRoundTrip);
    runTest(testWirelessLinkMatchesAWire);
    runTest(testWirelessLinkScoping);
    runTest(testWirelessLinkDiagnostics);
}
