#pragma once

#include "model/Types.h"

#include <functional>
#include <string>
#include <vector>

namespace simupy {

struct PackageStatus {
    bool installed = false;
    /// Empty when the module carries no __version__.
    std::string version;
    /// Where it was imported from, so a duplicate install is visible.
    std::string location;
};

/// The modules a block's source imports, as an aid to declaring them.
///
/// Reads `import x`, `from x import y` and `require("x", "pkg")`, skips the
/// standard library and simupy itself, and takes the pip name from require()
/// when it gives one. Static reading, so a conditional import inside a
/// function is still found but one built from a string is not — hence an aid,
/// not the record.
std::vector<PackageRequirement> detectRequirements(const std::string& source);

/// Python packages SimuPy installs for itself.
///
/// Never the system's: a distribution's site-packages is not ours to write to,
/// and on Ubuntu pip refuses outright (PEP 668). Everything lands in a
/// directory under the user's data folder, which is writable in every
/// deployment — Flatpak included, where /app is not — and is put on sys.path at
/// startup.
class PythonPackages {
public:
    static PythonPackages& instance();

    /// Where SimuPy keeps them. Only the path: starting the application must
    /// not leave a folder in someone's home for a feature they never used.
    const std::string& directory() const;

    /// Whether `module` imports, and from where.
    PackageStatus status(const std::string& module) const;

    /// True once the installer itself is available; see fetchInstaller().
    bool canInstall() const;

    /// Downloads pip's official zipapp into the package directory. Needs the
    /// network, and is only done when the user asks to install something.
    /// Returns an empty string on success, the reason otherwise.
    std::string fetchInstaller();

    /// Installs `spec` into directory(). `onOutput` receives pip's own lines as
    /// they come. Returns an empty string on success, the reason otherwise.
    std::string install(const std::string& spec,
                        const std::function<void(const std::string&)>& onOutput);

    /// Why an install of `spec` most likely failed, in one sentence, or empty
    /// when nothing useful can be said. Asks PyPI which interpreters the
    /// package ships wheels for: no wheel means pip fell back to building from
    /// source, which is where nearly every failure comes from.
    std::string explainFailure(const std::string& spec) const;

private:
    PythonPackages() = default;

    /// Made only when something is about to be written there.
    bool createDirectory() const;

    mutable std::string directory_;
};

}
