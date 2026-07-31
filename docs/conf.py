"""Sphinx configuration.

    pip install sphinx furo
    sphinx-build -b html docs docs/_build/html
"""

project = "SimuPy"
author = "SimuPy"
copyright = "2026 SimuPy contributors, GPL-3.0-or-later"
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

# Falls back to Sphinx's own theme, so the docs build without extras.
try:
    import furo
except ImportError:
    html_theme = "alabaster"

rst_prolog = """
.. |spy| replace:: ``.spy``
.. |spylib| replace:: ``.spylib``
"""

todo_include_todos = False
nitpicky = False
