#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace simupy {

/// Undo by snapshot rather than by command: one edit can touch blocks,
/// wires, geometry, parameters and subsystem contents at once.
class UndoHistory {
public:
    void reset(std::string state);

    /// Records a state, dropping anything previously undone past this point.
    void record(std::string state);

    bool canUndo() const { return cursor_ > 0; }
    bool canRedo() const { return cursor_ + 1 < states_.size(); }

    /// Only valid when the matching canUndo()/canRedo() is true.
    const std::string& undo();
    const std::string& redo();

    const std::string& current() const;

private:
    static constexpr std::size_t kLimit = 100;

    std::vector<std::string> states_;
    std::size_t cursor_ = 0;
};

}
