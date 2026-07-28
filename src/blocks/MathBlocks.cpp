#include "BlockUtils.h"

#include <cmath>

namespace simupy {
namespace {

class ElementwiseBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        width_ = s.inheritedWidth();
        s.outputWidths[0] = width_;
        configure();
    }

protected:
    virtual void configure() {}
    int width_ = 1;
};

class GainBlock : public ElementwiseBlock {
public:
    void setup(BlockSetup& s) override {
        const std::vector<double> gain = params().vector("gain", {1.0});
        width_ = std::max(s.inheritedWidth(), static_cast<int>(gain.size()));
        gain_ = broadcast(gain, width_, "the gain");
        s.outputWidths[0] = width_;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0) = c.in(0).cwiseProduct(gain_);
    }

private:
    Vec gain_;
};

class SumBlock : public Block {
public:
    PortLayout ports() const override {
        const std::string signs = signString();
        std::vector<std::string> inputs;
        for (char sign : signs) inputs.push_back(std::string(1, sign));
        return {inputs, {"out"}};
    }

    void setup(BlockSetup& s) override {
        const std::string signs = signString();
        signs_.clear();
        for (char sign : signs) signs_.push_back(sign == '-' ? -1.0 : 1.0);

        int width = 1;
        for (int i = 0; i < s.inputCount(); ++i)
            if (s.inputConnected[i]) width = std::max(width, s.inputWidths[i]);

        for (int i = 0; i < s.inputCount(); ++i) {
            if (!s.inputConnected[i]) continue;
            const int w = s.inputWidths[i];
            if (w != width && w != 1)
                throw ModelError("input " + std::to_string(i + 1) + " is " +
                                 std::to_string(w) + " wide but the block sums " +
                                 std::to_string(width) + "-wide signals");
        }

        width_ = width;
        s.outputWidths[0] = width;
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        y.setZero();
        for (int i = 0; i < c.nu && i < static_cast<int>(signs_.size()); ++i) {
            const Vec& u = c.in(i);
            if (u.size() == y.size())
                y += signs_[i] * u;
            else if (u.size() == 1)
                y.array() += signs_[i] * u[0];
        }
    }

private:
    std::string signString() const {
        std::string signs = params().text("signs", "++");
        if (signs.empty()) signs = "+";
        return signs;
    }

    std::vector<double> signs_;
    int width_ = 1;
};

class ProductBlock : public Block {
public:
    PortLayout ports() const override {
        const std::string ops = opString();
        std::vector<std::string> inputs;
        for (char op : ops) inputs.push_back(std::string(1, op));
        return {inputs, {"out"}};
    }

    void setup(BlockSetup& s) override {
        const std::string ops = opString();
        divide_.clear();
        for (char op : ops) divide_.push_back(op == '/');

        int width = 1;
        for (int i = 0; i < s.inputCount(); ++i)
            if (s.inputConnected[i]) width = std::max(width, s.inputWidths[i]);
        s.outputWidths[0] = width;
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        y.setOnes();
        for (int i = 0; i < c.nu && i < static_cast<int>(divide_.size()); ++i) {
            const Vec& u = c.in(i);
            if (divide_[i]) {
                if (u.size() == y.size())
                    y = y.cwiseQuotient(u);
                else if (u.size() == 1)
                    y /= u[0];
            } else {
                if (u.size() == y.size())
                    y = y.cwiseProduct(u);
                else if (u.size() == 1)
                    y *= u[0];
            }
        }
    }

private:
    std::string opString() const {
        std::string ops = params().text("operations", "**");
        if (ops.empty()) ops = "*";
        return ops;
    }

    std::vector<bool> divide_;
};

class AbsBlock : public ElementwiseBlock {
public:
    void setup(BlockSetup& s) override {
        ElementwiseBlock::setup(s);
        s.zeroCrossings = width_;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0) = c.in(0).cwiseAbs();
    }

    void computeZeroCrossings(const EvalContext& c,
                              Eigen::Ref<Vec> zc) override {
        const Vec& u = c.in(0);
        for (int i = 0; i < width_; ++i)
            zc[i] = u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
    }
};

class SignBlock : public ElementwiseBlock {
public:
    void setup(BlockSetup& s) override {
        ElementwiseBlock::setup(s);
        s.zeroCrossings = width_;
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        const Vec& u = c.in(0);
        for (Eigen::Index i = 0; i < y.size(); ++i)
            y[i] = (u[i] > 0.0) - (u[i] < 0.0);
    }

    void computeZeroCrossings(const EvalContext& c,
                              Eigen::Ref<Vec> zc) override {
        const Vec& u = c.in(0);
        for (int i = 0; i < width_; ++i)
            zc[i] = u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
    }
};

class MathFunctionBlock : public ElementwiseBlock {
public:
    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        const Vec& u = c.in(0);
        switch (function_) {
            case Function::Exp: y = u.array().exp(); break;
            case Function::Log: y = u.array().log(); break;
            case Function::Log10: y = u.array().log10(); break;
            case Function::Square: y = u.array().square(); break;
            case Function::Sqrt: y = u.array().sqrt(); break;
            case Function::Reciprocal: y = u.array().inverse(); break;
            case Function::Power: y = u.array().pow(exponent_); break;
        }
    }

protected:
    void configure() override {
        const std::string name = params().text("function", "exp");
        if (name == "exp") function_ = Function::Exp;
        else if (name == "log") function_ = Function::Log;
        else if (name == "log10") function_ = Function::Log10;
        else if (name == "square") function_ = Function::Square;
        else if (name == "sqrt") function_ = Function::Sqrt;
        else if (name == "1/u") function_ = Function::Reciprocal;
        else if (name == "pow") function_ = Function::Power;
        else throw ModelError("unknown math function '" + name + "'");
        exponent_ = params().real("exponent", 2.0);
    }

private:
    enum class Function { Exp, Log, Log10, Square, Sqrt, Reciprocal, Power };
    Function function_ = Function::Exp;
    double exponent_ = 2.0;
};

class TrigonometryBlock : public ElementwiseBlock {
public:
    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        const Vec& u = c.in(0);
        switch (function_) {
            case Function::Sin: y = u.array().sin(); break;
            case Function::Cos: y = u.array().cos(); break;
            case Function::Tan: y = u.array().tan(); break;
            case Function::Asin: y = u.array().asin(); break;
            case Function::Acos: y = u.array().acos(); break;
            case Function::Atan: y = u.array().atan(); break;
            case Function::Sinh: y = u.array().sinh(); break;
            case Function::Cosh: y = u.array().cosh(); break;
            case Function::Tanh: y = u.array().tanh(); break;
        }
    }

protected:
    void configure() override {
        const std::string name = params().text("function", "sin");
        if (name == "sin") function_ = Function::Sin;
        else if (name == "cos") function_ = Function::Cos;
        else if (name == "tan") function_ = Function::Tan;
        else if (name == "asin") function_ = Function::Asin;
        else if (name == "acos") function_ = Function::Acos;
        else if (name == "atan") function_ = Function::Atan;
        else if (name == "sinh") function_ = Function::Sinh;
        else if (name == "cosh") function_ = Function::Cosh;
        else if (name == "tanh") function_ = Function::Tanh;
        else throw ModelError("unknown trigonometric function '" + name + "'");
    }

private:
    enum class Function { Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh };
    Function function_ = Function::Sin;
};

class SaturationBlock : public ElementwiseBlock {
public:
    void setup(BlockSetup& s) override {
        ElementwiseBlock::setup(s);
        s.zeroCrossings = 2 * width_;
    }

    void computeOutputs(const EvalContext& c) override {
        c.out(0) = c.in(0).cwiseMax(lower_).cwiseMin(upper_);
    }

    void computeZeroCrossings(const EvalContext& c,
                              Eigen::Ref<Vec> zc) override {
        const Vec& u = c.in(0);
        for (int i = 0; i < width_; ++i) {
            const double value = u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
            zc[2 * i] = value - upper_;
            zc[2 * i + 1] = value - lower_;
        }
    }

protected:
    void configure() override {
        upper_ = params().real("upperLimit", 1.0);
        lower_ = params().real("lowerLimit", -1.0);
        if (lower_ > upper_)
            throw ModelError("the lower limit is above the upper limit");
    }

private:
    double upper_ = 1.0, lower_ = -1.0;
};

class DeadZoneBlock : public ElementwiseBlock {
public:
    void setup(BlockSetup& s) override {
        ElementwiseBlock::setup(s);
        s.zeroCrossings = 2 * width_;
    }

    void computeZeroCrossings(const EvalContext& c,
                              Eigen::Ref<Vec> zc) override {
        const Vec& u = c.in(0);
        for (int i = 0; i < width_; ++i) {
            const double value = u.size() == 1 ? u[0] : (i < u.size() ? u[i] : 0.0);
            zc[2 * i] = value - upper_;
            zc[2 * i + 1] = value - lower_;
        }
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        const Vec& u = c.in(0);
        for (Eigen::Index i = 0; i < y.size(); ++i) {
            if (u[i] > upper_) y[i] = u[i] - upper_;
            else if (u[i] < lower_) y[i] = u[i] - lower_;
            else y[i] = 0.0;
        }
    }

protected:
    void configure() override {
        upper_ = params().real("upperLimit", 0.5);
        lower_ = params().real("lowerLimit", -0.5);
    }

private:
    double upper_ = 0.5, lower_ = -0.5;
};

class MinMaxBlock : public Block {
public:
    PortLayout ports() const override {
        return {numberedPorts("in", std::max(1, params().integer("inputs", 2))),
                {"out"}};
    }

    void setup(BlockSetup& s) override {
        maximum_ = params().text("operation", "min") == "max";
        int width = 1;
        for (int i = 0; i < s.inputCount(); ++i)
            if (s.inputConnected[i]) width = std::max(width, s.inputWidths[i]);
        s.outputWidths[0] = width;
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        if (c.nu == 0) {
            y.setZero();
            return;
        }
        y = c.in(0).size() == y.size() ? c.in(0)
                                       : Vec::Constant(y.size(), c.in(0)[0]);
        for (int i = 1; i < c.nu; ++i) {
            const Vec& u = c.in(i);
            const Vec broadcasted =
                u.size() == y.size() ? u : Vec::Constant(y.size(), u[0]);
            if (maximum_)
                y = y.cwiseMax(broadcasted);
            else
                y = y.cwiseMin(broadcasted);
        }
    }

private:
    bool maximum_ = false;
};

class RelationalOperatorBlock : public Block {
public:
    PortLayout ports() const override { return {{"a", "b"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        operator_ = params().text("operator", ">");
        width_ = s.inheritedWidth();
        s.outputWidths[0] = width_;
        s.zeroCrossings = width_;
    }

    void computeZeroCrossings(const EvalContext& c,
                              Eigen::Ref<Vec> zc) override {
        const Vec& a = c.in(0);
        const Vec& b = c.in(1);
        for (int i = 0; i < width_; ++i) {
            const double lhs = a.size() == 1 ? a[0] : (i < a.size() ? a[i] : 0.0);
            const double rhs = b.size() == 1 ? b[0] : (i < b.size() ? b[i] : 0.0);
            zc[i] = lhs - rhs;
        }
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        const Vec& a = c.in(0);
        const Vec& b = c.in(1);
        for (Eigen::Index i = 0; i < y.size(); ++i) {
            const double lhs = a.size() == 1 ? a[0] : a[i];
            const double rhs = b.size() == 1 ? b[0] : b[i];
            bool result = false;
            if (operator_ == ">") result = lhs > rhs;
            else if (operator_ == ">=") result = lhs >= rhs;
            else if (operator_ == "<") result = lhs < rhs;
            else if (operator_ == "<=") result = lhs <= rhs;
            else if (operator_ == "==") result = lhs == rhs;
            else if (operator_ == "!=") result = lhs != rhs;
            y[i] = result ? 1.0 : 0.0;
        }
    }

private:
    std::string operator_ = ">";
    int width_ = 1;
};

class LogicalOperatorBlock : public Block {
public:
    PortLayout ports() const override {
        const bool unary = params().text("operator", "AND") == "NOT";
        return {numberedPorts("in", unary ? 1
                                          : std::max(1, params().integer(
                                                            "inputs", 2))),
                {"out"}};
    }

    void setup(BlockSetup& s) override {
        operator_ = params().text("operator", "AND");
        s.outputWidths[0] = s.inheritedWidth();
    }

    void computeOutputs(const EvalContext& c) override {
        Vec& y = c.out(0);
        for (Eigen::Index i = 0; i < y.size(); ++i) {
            auto value = [&](int port) {
                const Vec& u = c.in(port);
                return (u.size() == 1 ? u[0] : u[i]) != 0.0;
            };

            bool result;
            if (operator_ == "NOT") {
                result = !value(0);
            } else if (operator_ == "AND") {
                result = true;
                for (int p = 0; p < c.nu; ++p) result = result && value(p);
            } else if (operator_ == "OR") {
                result = false;
                for (int p = 0; p < c.nu; ++p) result = result || value(p);
            } else if (operator_ == "XOR") {
                result = false;
                for (int p = 0; p < c.nu; ++p) result = result != value(p);
            } else {
                bool combined = operator_ == "NAND";
                for (int p = 0; p < c.nu; ++p)
                    combined = operator_ == "NAND" ? (combined && value(p))
                                                   : (combined || value(p));
                result = !combined;
            }
            y[i] = result ? 1.0 : 0.0;
        }
    }

private:
    std::string operator_ = "AND";
};

class SumOfElementsBlock : public Block {
public:
    PortLayout ports() const override { return {{"in"}, {"out"}}; }

    void setup(BlockSetup& s) override {
        mode_ = params().text("operation", "sum");
        s.outputWidths[0] = 1;
    }

    void computeOutputs(const EvalContext& c) override {
        const Vec& u = c.in(0);
        if (mode_ == "sum") c.out(0)[0] = u.sum();
        else if (mode_ == "product") c.out(0)[0] = u.prod();
        else if (mode_ == "mean") c.out(0)[0] = u.size() ? u.mean() : 0.0;
        else if (mode_ == "norm") c.out(0)[0] = u.norm();
        else if (mode_ == "max") c.out(0)[0] = u.size() ? u.maxCoeff() : 0.0;
        else c.out(0)[0] = u.size() ? u.minCoeff() : 0.0;
    }

private:
    std::string mode_ = "sum";
};

}

void registerMathBlocks() {
    registerBlockType<GainBlock>(
        "Gain", "Math", "Multiplies the input element-wise by a constant.",
        {vectorParam("gain", "Gain", {1.0},
                     "A scalar, or one value per signal element.")},
        70.0, 50.0);

    registerBlockType<SumBlock>(
        "Sum", "Math", "Adds and subtracts its inputs.",
        {textParam("signs", "Signs", "+-",
                   "One character per input: '+' or '-'. \"++-\" makes three "
                   "inputs.")},
        60.0, 60.0);

    registerBlockType<ProductBlock>(
        "Product", "Math", "Multiplies and divides its inputs element-wise.",
        {textParam("operations", "Operations", "**",
                   "One character per input: '*' or '/'.")},
        60.0, 60.0);

    registerBlockType<AbsBlock>("Abs", "Math", "Absolute value.", {}, 60.0, 50.0);

    registerBlockType<SignBlock>("Sign", "Math",
                                 "Returns -1, 0 or +1 according to the sign.",
                                 {}, 60.0, 50.0);

    registerBlockType<MathFunctionBlock>(
        "MathFunction", "Math", "Applies a scalar function element-wise.",
        {choiceParam("function", "Function",
                     {"exp", "log", "log10", "square", "sqrt", "1/u", "pow"},
                     std::string("exp")),
         realParam("exponent", "Exponent (for pow)", 2.0)});

    registerBlockType<TrigonometryBlock>(
        "Trigonometry", "Math", "Trigonometric and hyperbolic functions.",
        {choiceParam("function", "Function",
                     {"sin", "cos", "tan", "asin", "acos", "atan", "sinh",
                      "cosh", "tanh"},
                     std::string("sin"))});

    registerBlockType<SaturationBlock>(
        "Saturation", "Math", "Clamps the signal between two limits.",
        {realParam("upperLimit", "Upper limit", 1.0),
         realParam("lowerLimit", "Lower limit", -1.0)});

    registerBlockType<DeadZoneBlock>(
        "DeadZone", "Math", "Outputs zero inside a band around the origin.",
        {realParam("upperLimit", "Upper limit", 0.5),
         realParam("lowerLimit", "Lower limit", -0.5)});

    registerBlockType<MinMaxBlock>(
        "MinMax", "Math", "Element-wise minimum or maximum of several inputs.",
        {choiceParam("operation", "Operation", {"min", "max"},
                     std::string("min")),
         intParam("inputs", "Number of inputs", 2, 1, 32)});

    registerBlockType<RelationalOperatorBlock>(
        "Relational", "Logic", "Compares two signals, returning 0 or 1.",
        {choiceParam("operator", "Operator", {">", ">=", "<", "<=", "==", "!="},
                     std::string(">"))});

    registerBlockType<LogicalOperatorBlock>(
        "Logic", "Logic", "Boolean combination of its inputs.",
        {choiceParam("operator", "Operator",
                     {"AND", "OR", "NOT", "NAND", "NOR", "XOR"},
                     std::string("AND")),
         intParam("inputs", "Number of inputs", 2, 1, 32)});

    registerBlockType<SumOfElementsBlock>(
        "VectorReduce", "Math", "Reduces a vector signal to a single value.",
        {choiceParam("operation", "Operation",
                     {"sum", "product", "mean", "norm", "max", "min"},
                     std::string("sum"))},
        90.0, 50.0);
}

}
