#include "model/BlockRegistry.h"
#include "io/CustomBlock.h"
#include "model/Model.h"
#include "engine/RealTimePacer.h"
#include "engine/Simulator.h"
#include "io/ModelSerializer.h"
#include "scripting/PythonEngine.h"

#include <atomic>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace simupy;

namespace {

void printUsage() {
    std::cout << "SimuPy " SIMUPY_VERSION "\n";
    std::cout << R"(Block-diagram simulation with Python blocks

Usage:
  simupy-cli run <model.spy> [options]     simulate a model
  simupy-cli blocks                        list the available block types
  simupy-cli info <model.spy>              show what a model contains
  simupy-cli libraries                     list installed custom block libraries

Options for `run`:
  --stop <t>          override the stop time
  --step <h>          override the fixed step size
  --solver <name>     rk45 | sdirk2 | rk4 | heun | euler | discrete
  --csv <path>        write all logged signals to a CSV file
  --realtime [factor] pace the run against the wall clock; the optional factor
                      is simulated seconds per real second (default 1)
  --unbounded         run until interrupted (Ctrl+C) rather than to a stop time
  --quiet             only report errors

Free software under the GNU General Public License, version 3 or, at your
option, any later version. There is no warranty; see the LICENSE file.
)";
}

std::atomic<bool> g_interrupted{false};

extern "C" void onInterrupt(int) { g_interrupted.store(true); }

std::string csvField(const std::string& text) {
    if (text.find_first_of(",\"\n\r") == std::string::npos) return text;

    std::string quoted = "\"";
    for (char c : text) {
        if (c == '"') quoted += '"';
        quoted += c;
    }
    return quoted + '"';
}

void writeCsv(const SignalLog& log, const std::string& path) {
    std::ofstream stream(path, std::ios::out | std::ios::trunc);
    if (!stream) throw ModelError("cannot write to '" + path + "'");
    stream << std::setprecision(10);

    stream << "time";
    for (const LogChannel& channel : log.channels())
        for (int c = 0; c < channel.width; ++c)
            stream << ',' << csvField(channel.label(c));
    stream << '\n';

    for (int i = 0; i < log.sampleCount(); ++i) {
        stream << log.times()[i];
        for (const LogChannel& channel : log.channels())
            for (int c = 0; c < channel.width; ++c)
                stream << ',' << channel.at(i, c);
        stream << '\n';
    }
}

int runModel(std::vector<std::string> args) {
    if (args.empty()) {
        std::cerr << "error: no model file given\n";
        return 2;
    }

    const std::string path = args.front();
    args.erase(args.begin());

    std::string csvPath;
    bool quiet = false;
    double stopTime = 0.0;
    double fixedStep = 0.0;
    std::string solverName;
    bool realTime = false;
    double realTimeFactor = 0.0;
    bool unbounded = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& flag = args[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= args.size())
                throw ModelError("option " + flag + " needs a value");
            return args[++i];
        };

        if (flag == "--stop") stopTime = std::stod(next());
        else if (flag == "--step") fixedStep = std::stod(next());
        else if (flag == "--solver") solverName = next();
        else if (flag == "--csv") csvPath = next();
        else if (flag == "--realtime") {
            realTime = true;
            if (i + 1 < args.size() && args[i + 1].rfind("--", 0) != 0)
                realTimeFactor = std::stod(args[++i]);
        }
        else if (flag == "--unbounded") unbounded = true;
        else if (flag == "--quiet") quiet = true;
        else throw ModelError("unknown option '" + flag + "'");
    }

    const std::filesystem::path modelPath(path);
    PythonEngine::instance().initialize(
        {std::filesystem::absolute(modelPath).parent_path().string()});

    PythonEngine::instance().setOutputHandler(
        [](const std::string& text, bool isError) {
            (isError ? std::cerr : std::cout) << text;
        });

    Model model;
    LoadReport report;
    ModelSerializer::load(model, path, &report);

    for (const std::string& dropped : report.droppedConnections)
        std::cerr << "warning: dropped wire " << dropped
                  << " — that input reads zero\n";

    if (stopTime > 0.0) model.solver().stopTime = stopTime;
    if (fixedStep > 0.0) model.solver().fixedStep = fixedStep;
    if (!solverName.empty())
        model.solver().method = SolverSettings::methodFromName(solverName);
    if (realTime) model.solver().realTime = true;
    if (realTimeFactor > 0.0) model.solver().realTimeFactor = realTimeFactor;
    if (unbounded) model.solver().unbounded = true;

    if (!model.initScript().empty())
        PythonEngine::instance().execute(model.initScript(), "<init script>");

    Simulator simulator(model, pythonExpressionEvaluator());
    simulator.initialize();

    for (const std::string& warning : simulator.compiled().warnings)
        std::cerr << "warning: " << warning << '\n';

    RealTimePacer pacer(model.solver().startTime, model.solver().realTimeFactor);
    const bool paced = model.solver().realTime;

    if (!quiet) {
        std::cout << "model    : " << model.name() << " (" << path << ")\n"
                  << "blocks   : " << model.blocks().size() << ", wires: "
                  << model.connections().size() << '\n'
                  << "solver   : " << simulator.solverName() << '\n'
                  << "states   : " << simulator.compiled().totalContinuousStates
                  << " continuous, "
                  << simulator.compiled().totalDiscreteStates << " discrete\n"
                  << "interval : [" << model.solver().startTime << ", ";
        if (model.solver().unbounded)
            std::cout << "until interrupted]\n";
        else
            std::cout << model.solver().stopTime << "] s\n";
        if (paced)
            std::cout << "pacing   : " << pacer.factor()
                      << "x real time\n";
        std::cout << std::flush;
    }

    std::signal(SIGINT, onInterrupt);

    while (simulator.step()) {
        if (g_interrupted.load()) break;
        if (paced) pacer.waitUntil(simulator.time(),
                                   [] { return g_interrupted.load(); });
    }
    simulator.terminate();
    std::signal(SIGINT, SIG_DFL);

    if (g_interrupted.load() && !quiet)
        std::cout << "\ninterrupted at t = " << simulator.time() << " s\n";

    if (!quiet) {
        std::cout << "\ndone in " << simulator.majorSteps() << " steps ("
                  << simulator.rejectedSteps() << " rejected, "
                  << simulator.derivativeEvaluations()
                  << " derivative evaluations)\n"
                  << "logged " << simulator.log().sampleCount()
                  << " samples across " << simulator.log().channels().size()
                  << " channels\n";

        if (simulator.smallestStep() > 0.0)
            std::cout << "step     : " << simulator.smallestStep() << " to "
                      << simulator.largestStep() << " s, worst error estimate "
                      << simulator.worstErrorNorm() << " of tolerance\n";
        if (simulator.locatedEvents() > 0)
            std::cout << "events   : " << simulator.locatedEvents()
                      << " located\n";

        if (paced) {
            if (pacer.heldRealTime())
                std::cout << "real time held, worst lag "
                          << pacer.worstLag() * 1000.0 << " ms\n";
            else
                std::cout << "COULD NOT KEEP UP: worst lag " << pacer.worstLag()
                          << " s, resynced " << pacer.resyncCount()
                          << " time(s) — the model is too slow for "
                          << pacer.factor() << "x real time\n";
        }

        for (const LogChannel& channel : simulator.log().channels()) {
            const int last = simulator.log().sampleCount() - 1;
            if (last < 0) continue;
            for (int c = 0; c < channel.width; ++c)
                std::cout << "  final  " << channel.label(c) << " = "
                          << channel.at(last, c) << '\n';
        }
    }

    if (!csvPath.empty()) {
        writeCsv(simulator.log(), csvPath);
        if (!quiet) std::cout << "wrote " << csvPath << '\n';
    }
    return 0;
}

int listBlocks() {
    registerBuiltinBlocks();
    const BlockRegistry& registry = BlockRegistry::instance();

    for (const std::string& category : registry.categories()) {
        std::cout << '\n' << category << '\n';
        for (const BlockType* type : registry.inCategory(category)) {
            std::cout << "  " << std::left << std::setw(22) << type->name
                      << type->description << '\n';
            for (const ParamSpec& param : type->params)
                std::cout << "      " << std::left << std::setw(18)
                          << param.name << param.label << '\n';
        }
    }
    return 0;
}

int listLibraries() {
    LibraryManager& manager = LibraryManager::instance();

    std::cout << "search path:\n";
    for (const std::string& path : manager.searchPaths())
        std::cout << "  " << path << '\n';

    const std::vector<CustomLibrary*> libraries = manager.libraries();
    if (libraries.empty()) {
        std::cout << "\nno block libraries installed\n";
        return 0;
    }

    for (const CustomLibrary* library : libraries) {
        std::cout << '\n'
                  << library->name << "  (revision " << library->revision
                  << ")\n  " << library->path << '\n';
        if (!library->description.empty())
            std::cout << "  " << library->description << '\n';

        for (const CustomBlockDef& def : library->blocks) {
            std::cout << "    " << std::left << std::setw(22) << def.name
                      << customBlockKindName(def.kind) << "  " << def.category
                      << '\n';
            for (const ParamSpec& param : def.params)
                std::cout << "        " << std::left << std::setw(18)
                          << param.name << param.label << '\n';
        }
    }
    return 0;
}

int showInfo(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "error: no model file given\n";
        return 2;
    }
    Model model;
    ModelSerializer::load(model, args.front());

    std::cout << "name   : " << model.name() << '\n'
              << "solver : " << SolverSettings::methodName(model.solver().method)
              << ", [" << model.solver().startTime << ", "
              << model.solver().stopTime << "] s\n"
              << "blocks :\n";
    for (const BlockPtr& block : model.blocks()) {
        const BlockGeometry& g = model.geometry(block->id());
        std::cout << "  " << std::left << std::setw(20) << block->name() << " ("
                  << std::setw(14) << block->typeName() << ") at " << g.x << ","
                  << g.y << "  size " << g.width << "x" << g.height
                  << (g.mirrored ? "  mirrored" : "") << '\n';
    }

    std::cout << "wires  :\n";
    for (const Connection& c : model.connections()) {
        const Block* source = model.block(c.sourceBlock);
        const Block* target = model.block(c.targetBlock);
        std::cout << "  " << (source ? source->name() : c.sourceBlock) << ':'
                  << c.sourcePort << "  ->  "
                  << (target ? target->name() : c.targetBlock) << ':'
                  << c.targetPort << '\n';
    }
    return 0;
}

}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help") {
        printUsage();
        return args.empty() ? 1 : 0;
    }

    const std::string command = args.front();
    args.erase(args.begin());

    registerBuiltinBlocks();

    try {
        for (const std::string& problem : LibraryManager::instance().loadAll())
            std::cerr << "warning: skipped block library " << problem << '\n';

        if (command == "run") return runModel(args);
        if (command == "blocks") return listBlocks();
        if (command == "libraries") return listLibraries();
        if (command == "info") return showInfo(args);

        std::cerr << "error: unknown command '" << command << "'\n\n";
        printUsage();
        return 2;
    } catch (const ModelError& error) {
        std::cerr << "\nerror: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "\nunexpected error: " << error.what() << '\n';
        return 1;
    }
}
