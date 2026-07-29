#include "Models.h"

#include <memory>

#include "blocks/SubsystemBlock.h"
#include "engine/RealTimePacer.h"
#include "engine/SignalLog.h"
#include "scripting/PythonEngine.h"

namespace harness {

using namespace simupy;

namespace {

// Kept alive past the call so `out` stays valid, but dropped the moment a run
// throws: the Simulator holds a reference to the caller's Model, which is
// usually a local about to go out of scope. Letting a half-initialised one
// survive means the next call destroys it — and terminates blocks that no
// longer exist.
std::unique_ptr<Simulator> g_keep;

template <typename Drive>
double run(Model& model, Simulator** out, Drive drive) {
    g_keep = std::make_unique<Simulator>(model, pythonExpressionEvaluator());
    try {
        g_keep->initialize();
        drive(*g_keep);
    } catch (...) {
        g_keep.reset();
        if (out) *out = nullptr;
        throw;
    }
    if (out) *out = g_keep.get();

    const SignalLog& log = g_keep->log();
    if (log.channels().empty() || log.sampleCount() == 0) return 0.0;
    return log.channels().front().at(log.sampleCount() - 1, 0);
}

}  // namespace

double runToEnd(Model& model, Simulator** out) {
    return run(model, out, [](Simulator& s) { s.run(); });
}

double runPaced(Model& model, Simulator** out) {
    const SolverSettings& settings = model.solver();
    return run(model, out, [&settings](Simulator& s) {
        RealTimePacer pacer(settings.startTime, settings.realTimeFactor);
        while (s.step()) pacer.waitUntil(s.time());
        s.terminate();
    });
}

Block* addAffineSubsystem(Model& model, double x, double y, double gain,
                          double offset) {
    Block* holder = model.addBlock("Subsystem", x, y);
    Model& inner = dynamic_cast<SubsystemBlock*>(holder)->contents();

    Block* in = inner.addBlock("Inport", 0, 0);
    in->params().set("portNumber", 1.0);
    Block* scale = inner.addBlock("Gain", 150, 0);
    scale->params().set("gain", std::vector<double>{gain});
    Block* bias = inner.addBlock("Constant", 0, 120);
    bias->params().set("value", std::vector<double>{offset});
    Block* sum = inner.addBlock("Sum", 300, 0);
    sum->params().set("signs", std::string("++"));
    Block* out = inner.addBlock("Outport", 450, 0);
    out->params().set("portNumber", 1.0);

    inner.connect(in->id(), 0, scale->id(), 0);
    inner.connect(scale->id(), 0, sum->id(), 0);
    inner.connect(bias->id(), 0, sum->id(), 1);
    inner.connect(sum->id(), 0, out->id(), 0);
    return holder;
}

}  // namespace harness
