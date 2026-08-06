#include "UndoHistory.h"

namespace simupy {
namespace {

const std::string kNothing;

}

void UndoHistory::reset(std::string state) {
    states_.clear();
    states_.push_back(std::move(state));
    cursor_ = 0;
}

void UndoHistory::record(std::string state) {
    if (states_.empty()) {
        states_.push_back(std::move(state));
        cursor_ = 0;
        return;
    }

    // A no-op edit must not fill the stack.
    if (state == states_[cursor_]) return;

    states_.resize(cursor_ + 1);
    states_.push_back(std::move(state));

    if (states_.size() > kLimit)
        states_.erase(states_.begin(), states_.begin() + (states_.size() - kLimit));

    cursor_ = states_.size() - 1;
}

const std::string& UndoHistory::undo() {
    if (!canUndo()) return kNothing;
    return states_[--cursor_];
}

const std::string& UndoHistory::redo() {
    if (!canRedo()) return kNothing;
    return states_[++cursor_];
}

const std::string& UndoHistory::current() const {
    return states_.empty() ? kNothing : states_[cursor_];
}

}
