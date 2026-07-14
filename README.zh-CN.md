# carbon-scheduler

[English](./README.md) | [简体中文](./README.zh-CN.md)

[![license](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.txt)

## 概述

为 Greenlet 协程提供通道(channel)和调度器(scheduler)。
tasklet 与通道的调度顺序及行为在设计上尽可能贴近 Stackless Python 的表现。

仅实现了 Stackless Python API 中 Carbon 所需要的功能。

## 🛠️ 构建

使用仓库根目录下提供的 `CMakeLists` 进行构建。

## 🔍 查阅文档

文档包含:
1. 自动生成的 Python API。
2. 自动生成的 C-API。
3. carbon-scheduler 使用指南。
4. carbon-scheduler 用法示例。

### 当前文档生成的环境要求

1. 文档可以在 Windows 或 macOS 上构建
2. 使用我们定制的 PythonInterpreter
3. 构建机器上需安装 Doxygen 1.12.0(或更高版本)
4. 构建分支的 /carbon/common/lib 下需要有以下库:
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

### 构建文档

文档构建的默认设置如下:
- TeamCity 构建代理:开启(ON)
- 本地开发构建:关闭(OFF)

如需覆盖默认的文档构建设置,请将 CMake 选项 `BUILD_DOCUMENTATION` 设为 `ON/OFF`。

构建 `INSTALL` 目标会构建全部文档,并将其放置在 `CMAKE_INSTALL_PREFIX` 指定的路径下。

文档的入口文件为 `documentation/index.html`。

文档源文件可以使用 .rst(reStructuredText)或 .md(Markdown)格式编写。

## 🤝 参与贡献

贡献遵循标准的 Git PR(Pull Request)流程。

修改 Python 或 C-API 的对外暴露内容时,请确保 docstring 和 C++ 文档注释块同步反映相应变更。

提交 Pull Request 或以其他方式为本项目做出贡献,即表示您同意以 MIT 许可证授权您的贡献内容,并确认您拥有这样做的权利。

## 📄 许可证与法律声明

本项目基于 [MIT 许可证](LICENSE.txt)授权。MIT 许可证中的任何条款均不授予任何关于 CCP Games 商标或游戏内容的权利。

版权声明:© 2025 CCP Games。
