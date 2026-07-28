#pragma once

#include "model/Model.h"

#include <memory>

namespace simupy {

class SubsystemBlock : public Block {
public:
    SubsystemBlock();
    ~SubsystemBlock() override;

    Model& contents() { return *contents_; }
    const Model& contents() const { return *contents_; }

    PortLayout ports() const override;
    void setup(BlockSetup& s) override;
    void computeOutputs(const EvalContext& c) override;

    std::vector<Block*> portBlocks(bool inputs) const;

private:
    std::unique_ptr<Model> contents_;
};

bool isSubsystem(const Block* block);
bool isInport(const Block* block);
bool isOutport(const Block* block);

int portNumberOf(const Block* block);

void registerSubsystemBlocks();

}
