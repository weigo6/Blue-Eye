# Blue-Eye

`Blue-Eye` 是一个基于 `STM32F407VGT6` 的嵌入式固件工程，用于采集两类串口传感器数据，并通过 `OLED` 屏进行本地显示。当前工程由 `STM32CubeMX` 生成底层初始化代码，构建系统使用 `CMake + Ninja + arm-none-eabi-gcc`。

## 项目简介

当前固件主要完成以下工作：

- 通过 `USART3` 轮询采集 `XDA-10F-100P-7` 四合一数字电极数据
- 通过 `USART3` 轮询采集 `RS485` 压力变送器数据
- 通过 `I2C1` 驱动 `SSD1306` 兼容 `128x64` OLED 显示屏
- 通过 `PA0` 按键实现页面切换和返回欢迎页
- 通过 `PB2` 指示灯反映当前界面状态

## 功能特性

- 双传感器轮询采集，主循环周期性读取设备数据
- OLED 多页面显示，展示实时数据和通信状态
- 按键单击切页、双击返回首页
- 基于 `Modbus-RTU` 的基础帧收发、CRC 校验和异常状态标记
- 保留 `STM32CubeMX` 工程文件，便于后续修改外设配置
- 使用 `CMakePresets.json` 管理 `Debug` / `Release` 构建

## 硬件组成

- 主控：`STM32F407VGT6`
- 显示：`0.96"`、`128x64`、`SSD1306` 兼容 `I2C` OLED
- 传感器 1：`XDA-10F-100P-7` 四合一数字电极
- 传感器 2：`RS485` 压力变送器
- 通信：`USART3` 外接 `RS485` 模块（自动收发）

## 默认通信参数

仓库当前代码中的默认配置如下：

| 对象 | 接口 | 默认参数 |
| --- | --- | --- |
| 调试串口 | `USART2` | `9600 8N1` |
| 传感器总线 | `USART3` | `9600 8N1` |
| XDA 传感器 | `USART3` | 从机地址 `0x02` |
| 压力传感器 | `USART3` | 从机地址 `0x01` |

说明：

- `XDA` 代码当前按固定地址和固定波特率工作，不做自动扫描
- 压力传感器地址 `0x01` 是代码默认值，是否与现场设备一致需要联调确认
- 两个传感器当前共用 `USART3`，需要保证总线拓扑、从机地址和轮询时序不冲突

## 页面与交互

- 上电后进入欢迎页
- 按键单击：`欢迎页 -> XDA 页 -> 压力页 -> XDA 页 ...` 循环切换
- 按键双击：返回欢迎页
- 欢迎页时 `PB2` 熄灭
- 数据页面时 `PB2` 点亮

显示内容包括：

- `XDA` 页面：`EC`、温度、`TDS`、盐度、从机地址、通信状态
- 压力页面：压力值、单位、小数位、原始值、从机地址、通信状态

## 引脚速览

| 功能 | 引脚 |
| --- | --- |
| 按键 `KEY` | `PA0` |
| 调试串口 `USART2_TX/RX` | `PA2` / `PA3` |
| 状态 LED | `PB2` |
| OLED `I2C1_SCL/SDA` | `PB6` / `PB7` |
| 传感器串口 `USART3_TX/RX` | `PD8` / `PD9` |
| `SDIO_D0~D3/CK/CMD` | `PC8`~`PC12` / `PD2` |

更完整的板级说明见：

- [docs/board_pin_config.md](./docs/board_pin_config.md)

## 软件架构

工程主要由两部分组成：

- `STM32CubeMX` 生成的底层外设初始化代码
- 用户自定义的传感器、按键、OLED 和 UI 逻辑

关键模块如下：

- `Core/Src/main.c`：主循环、外设初始化、任务调度入口
- `Core/Src/app_ui.c`：OLED 页面渲染和页面切换逻辑
- `Core/Src/key.c`：按键去抖、单击/双击识别
- `Core/Src/oled.c`：OLED 驱动
- `Core/Src/xda_sensor.c`：XDA 传感器 Modbus 读取逻辑
- `Core/Src/pressure_sensor.c`：压力传感器 Modbus 读取逻辑

主循环当前逻辑比较直接：

1. 初始化 GPIO、DMA、I2C、USART、SDIO
2. 初始化 OLED、XDA 传感器、压力传感器和 UI
3. 在 `while(1)` 中轮询两个传感器任务
4. 刷新 UI 数据
5. 处理按键事件

## 开发环境

建议准备以下工具：

- VS Code
- [STM32CubeIDE for Visual Studio Code 插件](https://marketplace.visualstudio.com/items?itemName=stmicroelectronics.stm32-vscode-extension)
- 可选：`STM32CubeMX` 或 `STM32CubeIDE`，用于重新生成 `.ioc` 对应的底层代码

## 目录结构

```text
blue-eye/
|-- Core/
|   |-- Inc/                # 用户头文件与 CubeMX 生成头文件
|   `-- Src/                # 应用代码与 CubeMX 生成源码
|-- Drivers/                # CMSIS 与 STM32 HAL 驱动
|-- cmake/                  # 交叉编译工具链与 CubeMX CMake 文件
|-- docs/                   # 协议文档、板级说明
|-- CMakeLists.txt          # 顶层构建入口
|-- CMakePresets.json       # Debug / Release 预设
|-- blue-eye.ioc            # STM32CubeMX 工程文件
|-- STM32F407xx_FLASH.ld    # 链接脚本
`-- startup_stm32f407xx.s   # 启动文件
```

## 相关文档

- [board_pin_config.md](./docs/board_pin_config.md)
- [RS485 压力变送器 Modbus-RTU 协议开发手册.md](./docs/RS485%20压力变送器%20Modbus-RTU%20协议开发手册.md)
- [XDA-10F-100P-7 四合一数字电极通信协议开发手册.md](./docs/XDA-10F-100P-7%20四合一数字电极通信协议开发手册.md)

## License

`STM32CubeMX` 生成代码与第三方驱动的版权和许可说明，请以各源文件头注释及 `Drivers/` 目录中的许可文件为准。
