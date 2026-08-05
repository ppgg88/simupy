#include "FlatModel.h"

#include "blocks/BlockUtils.h"
#include "blocks/SubsystemBlock.h"

#include <map>
#include <memory>
#include <set>

namespace simupy {
namespace {

struct Scope {
    Model* model = nullptr;
    Scope* parent = nullptr;
    Block* subsystem = nullptr;
    std::string prefix;
};

constexpr int kMaxDepth = 64;

using Environment = std::map<std::string, ParamValue>;

class Flattener {
public:
    explicit Flattener(const ExpressionEvaluator& evaluate) : evaluate_(evaluate) {}

    FlatModel run(Model& root) {
        walk(root, nullptr, nullptr, {}, 0, Environment{});
        connect();
        return std::move(flat_);
    }

private:
    /// Resolved top-down, so a mask parameter reaches nested custom blocks.
    void resolveExpressions(Block& block, const Environment& environment) {
        if (!evaluate_)
            throw ModelError(
                "'" + block.name() +
                "' has parameter expressions but no evaluator was installed");

        for (const auto& [name, expression] : block.paramExpressions())
            block.params().set(
                name, evaluate_(expression, environment,
                                "parameter '" + name + "' of " + block.name()));
    }

    void registerTag(Block* block, Scope* scope) {
        if (!isGoto(block)) return;

        const std::string tag = signalTagOf(block);
        if (tag.empty())
            throw ModelError("the Goto block '" + scope->prefix +
                             block->name() + "' has no tag");

        auto& local = localGotos_[scope];
        const auto clash = local.find(tag);
        if (clash != local.end())
            throw ModelError("'" + scope->prefix + block->name() + "' and '" +
                             scope->prefix + clash->second->name() +
                             "' are both Goto blocks tagged '" + tag +
                             "' in the same diagram, so a From tagged '" + tag +
                             "' would not know which one to read");
        local[tag] = block;
        gotoScope_[block] = scope;

        if (!isGlobalGoto(block)) return;

        const auto global = globalGotos_.find(tag);
        if (global != globalGotos_.end())
            throw ModelError("'" + scope->prefix + block->name() + "' and '" +
                             globalPath_[tag] +
                             "' are both global Goto blocks tagged '" + tag +
                             "'; a global tag has to be unique across the "
                             "whole model");
        globalGotos_[tag] = block;
        globalPath_[tag] = scope->prefix + block->name();
    }

    Block* gotoFor(Block* from, Scope* scope) const {
        const std::string tag = signalTagOf(from);
        if (tag.empty())
            throw ModelError("the From block '" + scope->prefix + from->name() +
                             "' has no tag");

        const auto locals = localGotos_.find(scope);
        if (locals != localGotos_.end()) {
            const auto found = locals->second.find(tag);
            if (found != locals->second.end()) return found->second;
        }

        const auto global = globalGotos_.find(tag);
        if (global != globalGotos_.end()) return global->second;

        throw ModelError("the From block '" + scope->prefix + from->name() +
                         "' reads tag '" + tag +
                         "', but no Goto in the same diagram carries it and "
                         "no global Goto does either");
    }

    void walk(Model& model, Scope* parent, Block* subsystem,
              const std::string& prefix, int depth,
              const Environment& environment) {
        if (depth > kMaxDepth)
            throw ModelError(
                "the subsystem hierarchy is nested more than " +
                std::to_string(kMaxDepth) +
                " deep, or a subsystem contains itself");

        auto owned = std::make_unique<Scope>();
        owned->model = &model;
        owned->parent = parent;
        owned->subsystem = subsystem;
        owned->prefix = prefix;
        Scope* scope = owned.get();
        scopes_.push_back(std::move(owned));

        if (subsystem) contentsScope_[subsystem] = scope;

        // First: a parameter can decide how many ports a block has.
        for (const BlockPtr& held : model.blocks())
            if (!held->paramExpressions().empty())
                resolveExpressions(*held, environment);

        model.pruneDanglingConnections();

        std::set<int> inputNumbers;
        std::set<int> outputNumbers;

        for (const BlockPtr& held : model.blocks()) {
            Block* block = held.get();
            scopeOf_[block] = scope;

            if (isSubsystem(block)) {
                auto* sub = static_cast<SubsystemBlock*>(block);
                walk(sub->contents(), scope, block,
                     prefix + block->name() + "/", depth + 1,
                     block->params().all());
                continue;
            }

            if (isInport(block) || isOutport(block)) {
                std::set<int>& numbers =
                    isInport(block) ? inputNumbers : outputNumbers;
                if (parent && !numbers.insert(portNumberOf(block)).second)
                    throw ModelError(
                        "subsystem '" + subsystem->name() + "' has two " +
                        std::string(isInport(block) ? "Inport" : "Outport") +
                        " blocks numbered " +
                        std::to_string(portNumberOf(block)));
                if (parent) continue;
            }

            // Goto and From are aliases for a signal, not blocks to execute.
            if (isGoto(block) || isFrom(block)) {
                registerTag(block, scope);
                continue;
            }

            flatIndex_[block] = static_cast<int>(flat_.blocks.size());
            flat_.blocks.push_back({block, prefix + block->name()});
        }
    }

    bool resolveSource(Block* block, int port, Scope* scope, int& outBlock,
                       int& outPort) const {
        // A link chain that closes on itself has no signal in it at all.
        std::set<const Block*> visitedFroms;

        for (int hop = 0; hop <= kMaxDepth * 2; ++hop) {
            if (isFrom(block)) {
                if (!visitedFroms.insert(block).second)
                    throw ModelError(
                        "the wireless link tagged '" + signalTagOf(block) +
                        "' feeds itself: following it from '" + scope->prefix +
                        block->name() + "' comes back to the same From block "
                        "without passing through anything that computes a "
                        "value");

                Block* target = gotoFor(block, scope);
                Scope* gotoHome = gotoScope_.at(target);
                const Connection* wire =
                    gotoHome->model->incoming(target->id(), 0);
                if (!wire) return false;

                block = gotoHome->model->block(wire->sourceBlock);
                port = wire->sourcePort;
                scope = gotoHome;
                continue;
            }

            if (isSubsystem(block)) {
                auto* sub = static_cast<SubsystemBlock*>(block);
                const std::vector<Block*> outports = sub->portBlocks(false);
                if (port < 0 || port >= static_cast<int>(outports.size()) ||
                    !outports[port])
                    return false;

                const Connection* wire =
                    sub->contents().incoming(outports[port]->id(), 0);
                if (!wire) return false;

                Scope* inner = contentsScope_.at(block);
                block = inner->model->block(wire->sourceBlock);
                port = wire->sourcePort;
                scope = inner;
                continue;
            }

            if (isInport(block) && scope->parent) {
                const int slot = portNumberOf(block) - 1;
                const Connection* wire = scope->parent->model->incoming(
                    scope->subsystem->id(), slot);
                if (!wire) return false;

                block = scope->parent->model->block(wire->sourceBlock);
                port = wire->sourcePort;
                scope = scope->parent;
                continue;
            }

            auto found = flatIndex_.find(block);
            if (found == flatIndex_.end()) return false;
            outBlock = found->second;
            outPort = port;
            return true;
        }
        return false;
    }

    void connect() {
        for (const std::unique_ptr<Scope>& scope : scopes_) {
            for (const BlockPtr& held : scope->model->blocks()) {
                Block* block = held.get();
                if (isSubsystem(block)) continue;
                if (isGoto(block) || isFrom(block)) continue;
                if ((isInport(block) || isOutport(block)) && scope->parent)
                    continue;

                const auto target = flatIndex_.find(block);
                if (target == flatIndex_.end()) continue;

                const PortLayout layout = block->ports();
                for (int port = 0;
                     port < static_cast<int>(layout.inputs.size()); ++port) {
                    const Connection* wire =
                        scope->model->incoming(block->id(), port);
                    if (!wire) continue;

                    Block* source = scope->model->block(wire->sourceBlock);
                    int sourceBlock = 0;
                    int sourcePort = 0;
                    if (resolveSource(source, wire->sourcePort, scope.get(),
                                      sourceBlock, sourcePort)) {
                        flat_.connections.push_back(
                            {sourceBlock, sourcePort, target->second, port});
                    } else {
                        flat_.warnings.push_back(
                            "the wire into '" + scope->prefix + block->name() +
                            "' port " + std::to_string(port + 1) + " (" +
                            layout.inputs[port] +
                            ") leads nowhere: it was dropped and that input "
                            "reads zero. A subsystem outport with nothing "
                            "wired to it inside is the usual cause.");
                    }
                }
            }
        }
    }

    FlatModel flat_;
    std::vector<std::unique_ptr<Scope>> scopes_;
    std::map<const Block*, Scope*> scopeOf_;
    std::map<const Block*, Scope*> contentsScope_;
    std::map<const Block*, int> flatIndex_;

    std::map<const Scope*, std::map<std::string, Block*>> localGotos_;
    std::map<std::string, Block*> globalGotos_;
    std::map<std::string, std::string> globalPath_;
    std::map<const Block*, Scope*> gotoScope_;
    ExpressionEvaluator evaluate_;
};

}

FlatModel flattenModel(Model& model, const ExpressionEvaluator& evaluate) {
    model.pruneDanglingConnections();
    return Flattener(evaluate).run(model);
}

}
