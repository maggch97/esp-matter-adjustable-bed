# ESP Matter 电动床控制器

基于 [ESP-Matter](https://github.com/espressif/esp-matter) 的 Matter 智能电动床控制固件。通过 Matter 协议的 Window Covering 设备类型暴露两个电机端点，可接入 Apple Home、Google Home、Amazon Alexa 等支持 Matter 的智能家居平台，实现电动床头部和腿部的升降控制。

## 功能

- 通过 Matter Window Covering 集群控制两个独立电机（如床头电机、床尾电机）
- 支持位置百分比精确控制（0%~100%）
- 使用 FreeRTOS 定时器实现平滑运动控制
- 支持 Matter 设备发现与配网（BLE + Wi-Fi / Thread）
- 支持通用开关（Generic Switch）端点扩展

## 硬件

### 引脚定义

| 引脚 | 功能 |
|------|------|
| GPIO 10 | 电机1 正转继电器 |
| GPIO 11 | 电机1 反转继电器 |
| GPIO 12 | 电机2 正转继电器 |
| GPIO 13 | 电机2 反转继电器 |

### 支持的芯片

ESP32 / ESP32-S3 / ESP32-C3 / ESP32-C6 / ESP32-H2 等 ESP-IDF v5.x 支持的芯片。

## 项目结构

```
├── CMakeLists.txt              # 项目级 CMake 配置
├── README.md
└── main/
    ├── CMakeLists.txt          # 组件级 CMake 配置
    ├── idf_component.yml       # ESP-IDF 组件依赖声明
    ├── Kconfig.projbuild       # 项目配置选项
    ├── app_main.cpp            # 主程序：Matter 节点创建、电机控制逻辑
    ├── app_driver.cpp          # 按钮驱动及 Generic Switch 事件处理
    └── app_priv.h              # 私有头文件及类型定义
```

## 编译

### 前置要求

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/) v5.4.1 或更高版本
- ESP-IDF Component Manager（随 ESP-IDF v5.x 自带）

> **注意**：本项目通过 ESP-IDF Component Manager 自动从 [ESP Component Registry](https://components.espressif.com/) 下载 `esp_matter` 及其所有依赖，无需手动克隆 esp-matter 仓库或设置 `ESP_MATTER_PATH` 环境变量。

### 编译步骤

```bash
# 激活 ESP-IDF 环境
. $IDF_PATH/export.sh

# 设置目标芯片（以 ESP32-S3 为例）
idf.py set-target esp32s3

# 编译
idf.py build

# 烧录并打开串口监视器
idf.py -p /dev/ttyUSB0 flash monitor
```

## 配网

设备首次启动后会进入配网模式，可通过以下方式添加到 Matter 网络：

1. **Apple Home**：打开"家庭"App → 添加配件 → 扫描设备上的配对二维码
2. **Google Home**：打开 Google Home App → 添加设备 → Matter 设备
3. **chip-tool**（调试）：
   ```bash
   chip-tool pairing ble-wifi <node-id> <ssid> <password> 20202021 3840
   ```

## 工作原理

设备在 Matter 网络中注册为一个聚合器（Aggregator），下挂两个 Window Covering 端点：

- **端点 1**：电机1（如床头升降）
- **端点 2**：电机2（如床尾升降）

当智能家居平台发送目标位置命令时，固件将百分比转换为电机运行时间，通过继电器控制电机正反转。FreeRTOS 定时器以 100ms 为周期检查当前位置与目标位置的差值，驱动电机逐步运动至目标位置，并在到达后自动停止。

## 许可证

本项目代码基于 Public Domain (CC0) 许可。