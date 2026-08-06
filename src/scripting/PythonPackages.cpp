#include "PythonPackages.h"

#include "PythonEngine.h"
#include "PythonError.h"

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>

#include <cstdlib>
#include <filesystem>

namespace py = pybind11;

namespace simupy {
namespace {

namespace fs = std::filesystem;

/// pip's official zipapp: one file, no install, runs on any Python 3.
constexpr const char* kInstallerUrl = "https://bootstrap.pypa.io/pip/pip.pyz";
constexpr const char* kInstallerName = "pip.pyz";

std::string environmentPath(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::string dataDirectory() {
#ifdef _WIN32
    const std::string base = environmentPath("APPDATA");
    if (base.empty()) return "python-packages";
    return base + "/SimuPy/python-packages";
#else
    std::string base = environmentPath("XDG_DATA_HOME");
    if (base.empty()) {
        const std::string home = environmentPath("HOME");
        if (home.empty()) return "python-packages";
        base = home + "/.local/share";
    }
    return base + "/simupy/python-packages";
#endif
}

}

std::vector<PackageRequirement> detectRequirements(const std::string& source) {
    std::vector<PackageRequirement> found;
    if (source.empty() || !PythonEngine::instance().isReady()) return found;

    ScopedGil gil;
    try {
        py::dict scope;
        scope["source"] = source;
        py::exec(R"(
import ast, sys

# Nothing to install for these: they ship with Python, or with us.
known = set(sys.stdlib_module_names) | {"simupy", "numpy", "np"}

found = {}

def note(module, package=None, purpose=None):
    module = (module or "").split(".")[0]
    if not module or module in known or module.startswith("_"):
        return
    entry = found.setdefault(module, [module, "", ""])
    if package and not entry[1]:
        entry[1] = package
    if purpose and not entry[2]:
        entry[2] = purpose

try:
    tree = ast.parse(source)
except SyntaxError:
    tree = None

for node in ast.walk(tree) if tree else []:
    if isinstance(node, ast.Import):
        for alias in node.names:
            note(alias.name)
    elif isinstance(node, ast.ImportFrom):
        if node.level == 0:
            note(node.module)
    elif isinstance(node, ast.Call):
        name = getattr(node.func, "id", None) or getattr(node.func, "attr", None)
        if name != "require":
            continue
        literals = [a.value for a in node.args
                    if isinstance(a, ast.Constant) and isinstance(a.value, str)]
        if literals:
            note(*literals[:3])

detected = sorted(found.values())
)",
                 scope);

        for (const py::handle& row : scope["detected"]) {
            PackageRequirement need;
            need.module = row.cast<py::list>()[0].cast<std::string>();
            need.package = row.cast<py::list>()[1].cast<std::string>();
            need.purpose = row.cast<py::list>()[2].cast<std::string>();
            found.push_back(std::move(need));
        }
    } catch (const py::error_already_set&) {
        PyErr_Clear();
    }
    return found;
}

PythonPackages& PythonPackages::instance() {
    static PythonPackages packages;
    return packages;
}

const std::string& PythonPackages::directory() const {
    if (directory_.empty()) directory_ = dataDirectory();
    return directory_;
}

bool PythonPackages::createDirectory() const {
    std::error_code code;
    fs::create_directories(directory(), code);
    return !code;
}

PackageStatus PythonPackages::status(const std::string& module) const {
    PackageStatus result;
    if (module.empty() || !PythonEngine::instance().isReady()) return result;

    ScopedGil gil;
    try {
        py::module_ util = py::module_::import("importlib.util");
        py::object spec = util.attr("find_spec")(module);
        if (spec.is_none()) return result;

        result.installed = true;
        py::object origin = spec.attr("origin");
        if (!origin.is_none()) result.location = origin.cast<std::string>();

        // Importing is the only way to a version, and costs no more than the
        // block would when it runs.
        py::module_ imported = py::module_::import(module.c_str());
        if (py::hasattr(imported, "__version__"))
            result.version =
                py::str(imported.attr("__version__")).cast<std::string>();
    } catch (const py::error_already_set&) {
        PyErr_Clear();
    }
    return result;
}

bool PythonPackages::canInstall() const {
    std::error_code code;
    return fs::exists(fs::path(directory()) / kInstallerName, code);
}

std::string PythonPackages::fetchInstaller() {
    if (canInstall()) return {};
    if (!PythonEngine::instance().isReady())
        return "the Python interpreter is not running";
    if (!createDirectory())
        return "could not create " + directory();

    const std::string target =
        (fs::path(directory()) / kInstallerName).string();

    ScopedGil gil;
    try {
        py::dict scope;
        scope["url"] = kInstallerUrl;
        scope["target"] = target;
        py::exec(R"(
import urllib.request, os, tempfile

with urllib.request.urlopen(url, timeout=60) as response:
    payload = response.read()

if len(payload) < 100000:
    raise RuntimeError("the download is too small to be pip")

# Written beside the target then renamed, so an interrupted download cannot
# leave a half file that looks installed.
handle, temporary = tempfile.mkstemp(dir=os.path.dirname(target))
with os.fdopen(handle, "wb") as out:
    out.write(payload)
os.replace(temporary, target)
)",
                 scope);
    } catch (const py::error_already_set& error) {
        return "could not fetch the installer: " + describe(error);
    }
    return {};
}

std::string PythonPackages::install(
    const std::string& spec,
    const std::function<void(const std::string&)>& onOutput) {
    if (spec.empty()) return "nothing to install";
    if (!PythonEngine::instance().isReady())
        return "the Python interpreter is not running";

    if (const std::string problem = fetchInstaller(); !problem.empty())
        return problem;

    const std::string installer =
        (fs::path(directory()) / kInstallerName).string();

    ScopedGil gil;
    try {
        py::dict scope;
        scope["installer"] = installer;
        scope["target"] = directory();
        scope["spec"] = spec;

        // In-process: an embedded interpreter's sys.executable is this
        // application, not a Python that could be spawned.
        py::exec(R"(
import contextlib, io, runpy, sys

argv = sys.argv
sys.argv = ["pip", "install", "--target", target, "--upgrade",
            "--no-input", "--disable-pip-version-check", spec]
buffer = io.StringIO()
status = 0
try:
    with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
        runpy.run_path(installer, run_name="__main__")
except SystemExit as exit:
    status = exit.code or 0
except BaseException as error:
    status = 1
    buffer.write(str(error))
finally:
    sys.argv = argv

output = buffer.getvalue()
)",
                 scope);

        const std::string output = scope["output"].cast<std::string>();
        if (onOutput && !output.empty()) onOutput(output);

        const int status = scope["status"].cast<int>();
        if (status != 0)
            return "pip could not install '" + spec + "'";
    } catch (const py::error_already_set& error) {
        return "the installer failed: " + describe(error);
    }

    // A freshly installed package is not visible to an interpreter that has
    // already looked for it and failed.
    ScopedGil again;
    try {
        py::module_ importlib = py::module_::import("importlib");
        importlib.attr("invalidate_caches")();
    } catch (const py::error_already_set&) {
        PyErr_Clear();
    }
    return {};
}

}
