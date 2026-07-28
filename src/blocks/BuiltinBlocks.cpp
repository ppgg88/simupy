#include "scripting/PythonBlock.h"
#include "model/BlockRegistry.h"
#include "BlockUtils.h"
#include "ControlBlocks.h"
#include "SubsystemBlock.h"

namespace simupy {

void registerBuiltinBlocks() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    registerSourceBlocks();
    registerMathBlocks();
    registerContinuousBlocks();
    registerDiscreteBlocks();
    registerRoutingBlocks();
    registerControlBlocks();
    registerSinkBlocks();
    registerSubsystemBlocks();
    registerPythonBlocks();
}

}
