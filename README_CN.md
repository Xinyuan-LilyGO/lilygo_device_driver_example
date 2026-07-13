<h1 align="center">lilygo_device_driver_example</h1>

## **[英文](./README.md) | 中文**

[![Release](https://img.shields.io/github/v/release/Xinyuan-LilyGO/lilygo_device_driver_example?style=flat-square)](https://github.com/Xinyuan-LilyGO/lilygo_device_driver_example/releases)
[![License](https://img.shields.io/github/license/Xinyuan-LilyGO/lilygo_device_driver_example?style=flat-square)](./LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-Supported-ff6f00?style=flat-square)](https://github.com/espressif/esp-idf)

**lilygo_device_driver_example** 用于集中整理和维护
[`lilygo_device_driver`](https://github.com/Xinyuan-LilyGO/lilygo_device_driver)
相关的 ESP-IDF 示例。工程统一管理设备驱动示例，并将公共适配代码与各项测试
分开，方便后续继续增加设备和示例。

## 目录

- [功能特点](#功能特点)
- [示例](#示例)
- [快速开始](#快速开始)

## 功能特点

- 集中维护 `lilygo_device_driver` 相关示例。
- 不同测试使用独立的示例文件夹。
- 通过 `common` 组件提供公共设备适配。
- 通过配置菜单选择需要构建的设备和示例。

## 示例

全部示例源码位于 [`main/examples`](./main/examples) 文件夹。

需要测试时，在工程配置菜单中选择对应示例。

## 快速开始

### 克隆并初始化

```bash
git clone --recursive https://github.com/Xinyuan-LilyGO/lilygo_device_driver_example.git
cd lilygo_device_driver_example
```

如果克隆时没有使用 `--recursive`，请初始化子模块：

```bash
git submodule update --init --recursive
```

### 配置开发板

按照开发板要求设置 ESP-IDF 目标芯片，然后打开配置菜单：

```bash
idf.py set-target <target>
idf.py menuconfig
```

在 `menuconfig` 中依次操作：

1. 进入 `lilygo_device_driver configuration`。
2. 打开 `Select the device to build`，选择当前使用的开发板。
3. 进入 `Example Configuration`。
4. 打开 `Select the example to build`，选择需要测试的示例。
5. 根据所选示例配置其他必要选项。
6. 保存配置并退出。

### 构建

```bash
idf.py build
```
