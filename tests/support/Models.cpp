#include "Models.h"

#include "blocks/SubsystemBlock.h"
#include "engine/SignalLog.h"
#include "scripting/PythonEngine.h"

namespace harness {

using namespace simupy;

double runToEnd(Model& model, Simulator** out) {
    static Simulator* keep = nullptr;
    delete keep;
    keep = new Simulator(model, pythonExpressionEvaluator());
    keep->initialize();
    keep->run();
    if (out) *out = keep;

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
