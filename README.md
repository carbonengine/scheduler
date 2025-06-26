# carbon-scheduler

[![license](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.txt)

## Overview

Provides channels and a scheduler for Greenlet coroutines.
Tasklet and channel scheduling order and behaviour has been designed to match that of Stackless Python as closely as possible.

Only functionality required by Carbon has been implemented from the Stackless Python API.

## 🛠️ Building

Build using provided `CMakeLists` in the repository root.

## 🔍 Accessing the Documentation

Documentation provides:
1. Generated Python API.
2. Generated C-API.
3. carbon-scheduler guides.
4. carbon-scheduler usage examples.

### Current requirements for documentation generation

1. Documentation can be built on either Windows or macOS
2. Uses our custom PythonInterpreter
3. Doxygen 1.12.0 (or higher) installed on build machine
4. Following libraries available in /carbon/common/lib on build branch:
   - sphinx (7.4.7)
   - docutils (0.20.1)
   - pygments (2.18.0)
   - babel (2.16.0)
   - jinja2 (3.1.4)
   - snowballstemmer (2.2.0)
   - imagesize (1.4.1)
   - alabaster (0.7.16)
   - sphinxcontrib_applehelp (2.0.0)
   - sphinxcontrib_devhelp (2.0.0)
   - sphinxcontrib_htmlhelp (2.1.0)
   - sphinxcontrib_jquery (4.1)
   - sphinxcontrib_jsmath (1.0.1)
   - sphinxcontrib_qthelp (2.0.0)
   - sphinxcontrib_serializinghtml (2.0.0)
   - breathe (4.35.0)
   - sphinx_rtd_theme (2.0.0)
   - myst_parser (4.0.0)
   - markdown_it (3.0.0)
   - mdurl (0.1.2)
   - mdit_py_plugins (0.4.2)

### Building the documentation

Documentation is built using the following defaults:
- TeamCity build agents: ON
- Local dev build: OFF

To override the default documentation build settings, turn the CMake option `BUILD_DOCUMENTATION` to `ON/OFF`.

Building the `INSTALL` target will build all documentation and place it in the path specified by `CMAKE_INSTALL_PREFIX`.

Entry point for the documentation is `documentation/index.html`.

Documentation can be built using either .rst (restructuredText) or .md (markdown) files as sources.

## 🤝 Contributing

Contribution follows the standard GIT PR model.

When altering Python or C-API exposure ensure that docstrings and c++ documentation blocks reflect changes.

By submitting a pull request or otherwise contributing to this project, you agree to license your contribution under the MIT Licence, and you confirm that you have the right to do so.

## 📄 License and Legal Notices 

This project is licensed under the [MIT Licence](LICENSE.txt). Nothing in the MIT License grants any rights to CCP Games' trademarks or game content.

Copyright notice: © 2025 CCP Games.
