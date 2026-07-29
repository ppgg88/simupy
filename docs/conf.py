"""Sphinx configuration.

    pip install sphinx furo
    sphinx-build -b html docs docs/_build/html
"""

project = "SimuPy"
author = "SimuPy"
copyright = "SimuPy"
release = "0.0.1"
version = release

extensions = [
    "sphinx.ext.todo",
]

templates_path = ["_templates"]
exclude_patterns = ["_build"]

html_theme = "furo"
html_static_path = ["_static"]
html_title = f"SimuPy {release}"

# Falls back to the theme that ships with Sphinx, so the documentation builds
# without installing anything beyond Sphinx itself.
try:
    import furo  # noqa: F401
except ImportError:
    html_theme = "alabaster"

rst_prolog = """
.. |spy| replace:: ``.spy``
.. |spylib| replace:: ``.spylib``
"""

todo_include_todos = False
nitpicky = False
