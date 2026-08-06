#pragma once

#include <Eigen/Core>
#include <Eigen/LU>

#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace simupy {

inline constexpr double kPi = 3.14159265358979323846;

using Vec = Eigen::VectorXd;
using Mat = Eigen::MatrixXd;

inline constexpr int kInheritWidth = -1;

inline constexpr double kContinuousSampleTime = 0.0;
inline constexpr double kInheritedSampleTime = -1.0;

class ModelError : public std::runtime_error {
public:
    explicit ModelError(const std::string& what) : std::runtime_error(what) {}
};

using ParamValue = std::variant<double, bool, std::string, std::vector<double>>;

/// A Python package a model or a library needs.
struct PackageRequirement {
    /// What the code imports: `serial`.
    std::string module;
    /// What pip installs, when it differs: `pyserial`.
    std::string package;
    /// Shown to the user, so a missing package explains itself.
    std::string purpose;

    std::string installName() const {
        return package.empty() ? module : package;
    }

    bool operator==(const PackageRequirement& other) const {
        return module == other.module && package == other.package &&
               purpose == other.purpose;
    }
};

struct ParamSpec {
    enum class Kind { Real, Integer, Boolean, Text, Choice, Vector, PythonCode };

    std::string name;
    std::string label;
    Kind kind = Kind::Real;
    ParamValue defaultValue = 0.0;
    std::vector<std::string> choices;
    double minimum = -std::numeric_limits<double>::infinity();
    double maximum = std::numeric_limits<double>::infinity();
    std::string tooltip;
};

class Parameters {
public:
    bool has(const std::string& key) const { return values_.count(key) > 0; }

    void set(const std::string& key, ParamValue v) { values_[key] = std::move(v); }

    const std::map<std::string, ParamValue>& all() const { return values_; }
    std::map<std::string, ParamValue>& all() { return values_; }

    double real(const std::string& key, double fallback = 0.0) const {
        auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        if (auto* d = std::get_if<double>(&it->second)) return *d;
        if (auto* b = std::get_if<bool>(&it->second)) return *b ? 1.0 : 0.0;
        throw ModelError("parameter '" + key + "' is not a number");
    }

    int integer(const std::string& key, int fallback = 0) const {
        return static_cast<int>(std::llround(real(key, fallback)));
    }

    bool boolean(const std::string& key, bool fallback = false) const {
        auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        if (auto* b = std::get_if<bool>(&it->second)) return *b;
        if (auto* d = std::get_if<double>(&it->second)) return *d != 0.0;
        throw ModelError("parameter '" + key + "' is not a boolean");
    }

    std::string text(const std::string& key, const std::string& fallback = {}) const {
        auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        if (auto* s = std::get_if<std::string>(&it->second)) return *s;
        throw ModelError("parameter '" + key + "' is not a string");
    }

    /// Reads for drawing: a value of the wrong kind gives the fallback.
    double realOr(const std::string& key, double fallback) const {
        auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        if (auto* d = std::get_if<double>(&it->second)) return *d;
        if (auto* b = std::get_if<bool>(&it->second)) return *b ? 1.0 : 0.0;
        return fallback;
    }

    int integerOr(const std::string& key, int fallback) const {
        return static_cast<int>(std::llround(realOr(key, fallback)));
    }

    std::string textOr(const std::string& key,
                       const std::string& fallback) const {
        auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        if (auto* s = std::get_if<std::string>(&it->second)) return *s;
        return fallback;
    }

    std::vector<double> vectorOr(const std::string& key,
                                 std::vector<double> fallback) const {
        auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        if (auto* v = std::get_if<std::vector<double>>(&it->second)) return *v;
        if (auto* d = std::get_if<double>(&it->second)) return {*d};
        return fallback;
    }

    bool booleanOr(const std::string& key, bool fallback) const {
        auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        if (auto* b = std::get_if<bool>(&it->second)) return *b;
        if (auto* d = std::get_if<double>(&it->second)) return *d != 0.0;
        return fallback;
    }

    std::vector<double> vector(const std::string& key,
                               std::vector<double> fallback = {}) const {
        auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        if (auto* v = std::get_if<std::vector<double>>(&it->second)) return *v;
        if (auto* d = std::get_if<double>(&it->second)) return {*d};
        throw ModelError("parameter '" + key + "' is not a vector");
    }

    Vec eigenVector(const std::string& key, Vec fallback = Vec()) const {
        auto it = values_.find(key);
        if (it == values_.end()) return fallback;
        std::vector<double> v = vector(key);
        return Eigen::Map<const Vec>(v.data(), static_cast<Eigen::Index>(v.size()));
    }

private:
    std::map<std::string, ParamValue> values_;
};

}
