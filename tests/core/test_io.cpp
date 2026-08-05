
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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace simupy;

namespace {

std::string fileContents(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

void setEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

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

void testLibrarySearchPathSplitting() {
    beginTest("The library search path splits the way the platform writes it");

#ifdef _WIN32
    const char* separator = ";";
    const std::string first = "C:\\Program Files\\SimuPy\\libraries";
    const std::string second = "D:\\models\\blocks";
#else
    const char* separator = ":";
    const std::string first = "/usr/share/simupy/libraries";
    const std::string second = "/home/someone/blocks";
#endif

    const std::string joined = first + separator + second;
    setEnvironment("SIMUPY_LIBRARY_PATH", joined.c_str());

    const std::vector<std::string> paths =
        LibraryManager::instance().searchPaths();
    setEnvironment("SIMUPY_LIBRARY_PATH", nullptr);

    check(paths.size() >= 2, "both entries came back");
    if (paths.size() < 2) return;
    check(paths[0] == first, "the first is whole, drive letter and all");
    check(paths[1] == second, "and so is the second");
}

void testMalformedModelReportsRatherThanEscapes() {
    beginTest("A well-formed JSON with wrong types is reported, not fatal");

    // Valid JSON, wrong types: what used to escape as a non-ModelError.
    const std::vector<std::pair<const char*, const char*>> broken = {
        {"a non-numeric signal key",
         R"({"format":"simupy-model","version":1,"blocks":[
            {"id":"b1","type":"Constant","signals":{"nope":"x"}}]})"},
        {"a name that is not a string",
         R"({"format":"simupy-model","version":1,"blocks":[
            {"id":"b1","type":"Constant","name":42}]})"},
        {"a waypoint that is not a number",
         R"({"format":"simupy-model","version":1,
            "blocks":[{"id":"b1","type":"Constant"},
                      {"id":"b2","type":"Scope"}],
            "connections":[{"source":{"block":"b1","port":0},
                            "target":{"block":"b2","port":0},
                            "waypoints":[["x","y"]]}]})"},
        {"blocks that are not an array",
         R"({"format":"simupy-model","version":1,"blocks":7})"},
    };

    for (const auto& [what, text] : broken) {
        Model model;
        bool threwModelError = false;
        try {
            ModelSerializer::fromJson(text, model);
        } catch (const ModelError&) {
            threwModelError = true;
        } catch (const std::exception&) {
        }
        check(threwModelError,
              std::string("a model with ") + what + " raises a ModelError");
    }

    Model fine;
    bool threw = false;
    try {
        ModelSerializer::fromJson(
            R"({"format":"simupy-model","version":1,"blocks":[]})", fine);
    } catch (const std::exception&) {
        threw = true;
    }
    check(!threw, "an empty but valid model still loads");
}

void testMalformedLibraryIsSkippedNotFatal() {
    beginTest("A malformed library file is refused as an error, not a crash");

    const char* text =
        R"({"format":"simupy-library","version":1,"name":"Broken","blocks":[
            {"name":"Bad","kind":"subsystem","contents":{"blocks":[
                {"id":"b1","type":"Constant","signals":{"nope":"x"}}]}}]})";

    std::string message;
    bool threwModelError = false;
    try {
        CustomLibrary library;
        LibrarySerializer::fromJson(text, library);
    } catch (const ModelError& error) {
        threwModelError = true;
        message = error.what();
    } catch (const std::exception&) {
    }
    check(threwModelError, "the library error arrives as a ModelError");
    check(message.find("malformed") != std::string::npos,
          "and says the file is malformed rather than naming a C++ function");
}

void testFailedSaveKeepsThePreviousFile() {
    beginTest("A save that cannot complete leaves the previous file alone");

    namespace fs = std::filesystem;
    std::error_code code;
    const fs::path dir = fs::temp_directory_path() / "simupy-atomic-save";
    fs::remove_all(dir, code);
    fs::create_directories(dir, code);

    const fs::path target = dir / "model.spy";
    {
        std::ofstream seed(target);
        seed << "PREVIOUS";
    }

    Model model;
    model.addBlock("Constant", 0, 0);

    ModelSerializer::save(model, target.string());
    check(!fs::exists(target.string() + ".part"),
          "a good save leaves no temporary behind");
    check(fs::file_size(target, code) > 8, "and the model went in");

    // Root ignores the permission bit, so skip there rather than assert.
    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, code);
    bool stillWritable = false;
    {
        std::ofstream probe(dir / "probe");
        stillWritable = static_cast<bool>(probe);
    }
    fs::remove(dir / "probe", code);

    const std::string before = fileContents(target);

    if (stillWritable) {
        check(true, "skipped: the directory stayed writable, so this is root");
    } else {
        bool threw = false;
        try {
            model.addBlock("Gain", 100, 0);
            ModelSerializer::save(model, target.string());
        } catch (const ModelError&) {
            threw = true;
        }
        check(threw, "the failure is reported rather than swallowed");

        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace,
                        code);
        check(fileContents(target) == before,
              "and the file that was there is byte for byte what it was");
    }

    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, code);
    fs::remove_all(dir, code);
}

void testDroppedWiresAreReported() {
    beginTest("A wire that cannot be recreated is named, not silently lost");

    const char* text = R"({"format":"simupy-model","version":1,
        "blocks":[{"id":"b1","type":"Constant"}],
        "connections":[{"source":{"block":"b1","port":0},
                        "target":{"block":"ghost","port":0}}]})";

    Model model;
    LoadReport report;
    ModelSerializer::fromJson(text, model, &report);

    check(report.droppedConnections.size() == 1, "the dropped wire is reported");
    check(!report.clean(), "and the report no longer calls itself clean");
    if (!report.droppedConnections.empty())
        check(report.droppedConnections[0].find("ghost") != std::string::npos,
              "naming the end that could not be found");
}

void testUnresolvableSubsystemWireWarns() {
    beginTest("A subsystem outport with nothing inside warns at compile");

    Model model;
    Block* subsystem = model.addBlock("Subsystem", 0, 0);
    Block* scope = model.addBlock("Scope", 300, 0);

    // An outport nothing feeds: the wire is dropped and the Scope reads zeros.
    Model& inner = dynamic_cast<SubsystemBlock*>(subsystem)->contents();
    inner.addBlock("Outport", 0, 0);

    model.connect(subsystem->id(), 0, scope->id(), 0);
    model.solver().stopTime = 0.1;

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();

    const std::vector<std::string>& warnings = simulator.compiled().warnings;
    check(!warnings.empty(), "the unresolvable wire is reported");
    if (!warnings.empty())
        check(warnings[0].find("reads zero") != std::string::npos,
              "and says what the input does instead");
}

void runIoTests() {
    runTest(testCopyPasteRoundTrip);
    runTest(testCopyDropsHalfWires);
    runTest(testCopySubsystemTakesContents);
    runTest(testPasteTwice);
    runTest(testPasteRejectsRubbish);
    runTest(testMalformedModelReportsRatherThanEscapes);
    runTest(testMalformedLibraryIsSkippedNotFatal);
    runTest(testFailedSaveKeepsThePreviousFile);
    runTest(testDroppedWiresAreReported);
    runTest(testUnresolvableSubsystemWireWarns);
    runTest(testLibrarySearchPathSplitting);
}
