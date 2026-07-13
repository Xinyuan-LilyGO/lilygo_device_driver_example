<h1 align="center">lilygo_device_driver_example</h1>

## **English | [Chinese](./README_CN.md)**

[![Release](https://img.shields.io/github/v/release/Xinyuan-LilyGO/lilygo_device_driver_example?style=flat-square)](https://github.com/Xinyuan-LilyGO/lilygo_device_driver_example/releases)
[![License](https://img.shields.io/github/license/Xinyuan-LilyGO/lilygo_device_driver_example?style=flat-square)](./LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-Supported-ff6f00?style=flat-square)](https://github.com/espressif/esp-idf)

**lilygo_device_driver_example** collects and maintains ESP-IDF examples for
[`lilygo_device_driver`](https://github.com/Xinyuan-LilyGO/lilygo_device_driver).
It provides a unified place for device-driver examples and keeps shared
adaptation code separate from individual tests, making the project easier to
extend as new devices and examples are added.

## Table of Contents

- [Features](#features)
- [Examples](#examples)
- [Quick Start](#quick-start)

## Features

- Centralized examples for `lilygo_device_driver`.
- Independent example directories for different tests.
- Shared device adaptation through the `common` component.
- Menu-based selection of the device and example to build.

## Examples

All example source code is located in [`main/examples`](./main/examples).

Select the required example from the project configuration menu.

## Quick Start

### Clone and Initialize

```bash
git clone --recursive https://github.com/Xinyuan-LilyGO/lilygo_device_driver_example.git
cd lilygo_device_driver_example
```

If the repository was cloned without `--recursive`, initialize its submodules:

```bash
git submodule update --init --recursive
```

### Configure the Board

Set the ESP-IDF target required by the board, then open the configuration menu:

```bash
idf.py set-target <target>
idf.py menuconfig
```

In `menuconfig`:

1. Open `lilygo_device_driver configuration`.
2. Enter `Select the device to build` and select the board being used.
3. Open `Example Configuration`.
4. Enter `Select the example to build` and select the required example.
5. Configure any additional options required by that example.
6. Save the configuration and exit.

### Build

```bash
idf.py build
```
