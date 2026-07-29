#include "Models.h"

#include <memory>

#include "blocks/SubsystemBlock.h"
#include "engine/SignalLog.h"
#include "scripting/PythonEngine.h"

namespace harness {

using namespace simupy;

double runToEnd(Model& model, Simulator** out) {
    // Kept alive past the call so `out` stays valid, but dropped the moment a
    // run throws: the Simulator holds a reference to the caller's Model, which
    // is usually a local about to go out of scope. Letting a half-initialised
    // one survive means the next call destroys it — and terminates blocks that
    // no longer exist.
    static std::unique_ptr<Simulator> keep;

    keep = std::make_unique<Simulator>(model, pythonExpressionEvaluator());
    try {
        keep->initialize();
        keep->run();
    } catch (...) {
        keep.reset();
        if (out) *out = nullptr;
        throw;
    }
    if (out) *out = keep.get();

    const SignalLog& log = keep->log();
    if (log.channels().empty() || log.sampleCount() == 0) return 0.0;
    return log.channels().front().at(log.sampleCount() - 1, 0);
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
