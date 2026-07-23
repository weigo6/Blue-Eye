# Blue-Eye

`Blue-Eye` 是一套基于 `STM32F407VGT6` 的双传感器采集与现场记录系统：在同一条 `RS485 / Modbus-RTU` 总线上轮询四合一水质电极和压力变送器，将统一的周期快照同时送往 OLED、USART 遥测和 SD 卡日志，并提供配套 PC 端查看工具。

## 项目亮点

- **共享总线、单一所有者**：`ModbusBus` 独占 `USART3`，统一完成仲裁、CRC、超时、异常帧检查和结果分发，避免多个传感器驱动直接争用串口。
- **完整非阻塞通信链**：传感器访问采用 `TX IT -> TX 完成 -> Receive-to-IDLE DMA -> 主循环解析`，主循环无需等待串口收发。
- **统一 5 秒数据快照**：压力与 XDA 的最新样本、状态、样本时间和异常码被封装为同一份 `SensorRecord`，保证遥测与 SD 日志使用一致的数据视图。
- **稳定的绝对时间调度**：TIM6 每秒唤醒检查，5 秒事件按绝对截止时间推进；系统繁忙时合并过期周期并保留跳号，不伪造历史样本。
- **面向掉电和拔卡的日志设计**：FAT32 顺序文件、固定 64 字节记录、逐记录 CRC32、128 条 RAM 队列、512 字节批量写入、周期同步和安全卸载状态机。
- **端到端配套工具**：仓库内置实时遥测查看器和 SD 日志解析器，可完成组帧、校验、趋势查看、完整性检查以及 CSV/JSON 导出。

## 系统组成

| 模块 | 接口与默认配置 | 作用 |
| --- | --- | --- |
| XDA-10F-100P-7 四合一电极 | `USART3`，`9600 8N1`，地址 `0x02` | 采集 EC、温度、TDS、盐度 |
| RS485 压力变送器 | `USART3`，`9600 8N1`，地址 `0x01` | 支持 RAW / FLOAT 两种读取模式 |
| OLED | `I2C1`，SSD1306 兼容，`128x64` | 显示实时数据、通信和日志状态 |
| 遥测输出 | `USART1`，`9600 8N1` | 每 5 秒异步输出一帧 ASCII 快照 |
| SD 日志 | `SDIO 4-bit + FatFs` | 将快照写入 FAT32 SD 卡 |
| 人机交互 | `PA0` 按键、`PB2` LED | 页面切换、模式切换和安全卸载 |

详细接线与引脚定义见 [板级引脚配置](./docs/board_pin_config.md)。

## 软件架构

固件采用“**中断只通知，主循环推进状态机**”的协作式架构：

```text
传感器 -> ModbusBus -> 传感器数据模型 -> SensorRecord
                                              |-- USART1 DMA 遥测
                                              `-- SDIO / FatFs 日志

按键 EXTI -> KEY 事件 -> OLED 页面 / 运行配置
TIM6 IRQ  -> 周期检查 -> 5 秒统一快照
```

核心模块：

- `modbus_bus`：共享 USART3 的唯一所有者与 Modbus-RTU 事务层
- `xda_sensor` / `pressure_sensor`：轮询策略、数据解析和在线状态维护
- `periodic_trigger` / `sensor_record`：绝对周期调度与统一快照
- `telemetry_uart`：USART1 ASCII 帧封装和 DMA 异步发送
- `sd_logger`：FAT32 会话、队列、批量写入、滚动文件与安全卸载
- `key` / `app_ui` / `oled`：按键事件和多页面本地显示

完整调用链、状态机和 Mermaid 图见 [工程工作流程与工作原理](./docs/workflow_and_principles.md)。

## 页面与操作

- 单击：`欢迎页 -> XDA -> 压力 -> UART CONFIG -> SD LOGGER -> 欢迎页`
- 压力页长按约 `700 ms`：切换 `RAW` / `FLOAT` 读取模式
- `UART CONFIG` 页长按约 `700 ms`：开启或关闭 5 秒遥测
- `SD LOGGER` 页持续按住 `3 s`：安全卸载；在 `SAFE TO REMOVE` 或可恢复错误状态下再次长按可重新挂载
- 只有屏幕显示 `SAFE TO REMOVE` 后才可拔出 SD 卡

## 快速构建

建议准备以下工具：

- VS Code
- [STM32CubeIDE for Visual Studio Code 插件](https://marketplace.visualstudio.com/items?itemName=stmicroelectronics.stm32-vscode-extension)

外设配置源文件为 `blue-eye.ioc`，可使用 STM32CubeMX / STM32CubeIDE 调整并重新生成底层代码。

需要使用 STM32 官方工具链进行项目编译。

## 数据输出与 PC 工具

### USART1 实时遥测

固件每 5 秒输出一帧以 `$BE,` 开头、以 `*XX\r\n` 结尾的 ASCII 数据，`XX` 为 payload 的 XOR 校验。

配套查看器支持串口流式组帧、校验、噪声识别、实时数据展示和 CSV/JSON 导出：

- [遥测数据接口查看器](./tools/telemetry_viewer/README.md)

### SD 卡二进制日志

- 目录：`0:/LOG`
- 文件：`LOG00000.BIN` 起递增，单文件约 `32 MiB` 后滚动
- 文件头：`512` 字节
- 记录：固定 `64` 字节，每条包含 CRC32
- 正常写入：每 `8` 条组成一个 `512` 字节块

格式与解析工具：

- [SD 传感器日志格式](./docs/sensor_log_format.md)
- [SD 日志解析器](./tools/sd_log_reader/README.md)

## 目录概览

```text
blue-eye/
|-- Core/                   # 应用模块、CubeMX 初始化代码和中断入口
|-- FATFS/                  # FatFs 应用与 SDIO 磁盘适配层
|-- Drivers/                # STM32 HAL 与 CMSIS
|-- Middlewares/            # FatFs 中间件
|-- cmake/                  # 交叉编译工具链与 CubeMX CMake 配置
|-- docs/                   # 原理、板级、协议和日志格式文档
|-- tools/
|   |-- telemetry_viewer/   # USART1 实时遥测查看器
|   `-- sd_log_reader/      # SD 二进制日志解析器
|-- blue-eye.ioc            # STM32CubeMX 工程
|-- CMakeLists.txt
`-- CMakePresets.json
```

## 相关文档

- [工程工作流程与工作原理](./docs/workflow_and_principles.md)
- [板级引脚配置](./docs/board_pin_config.md)
- [SD 传感器日志格式](./docs/sensor_log_format.md)
- [RS485 压力变送器 Modbus-RTU 协议](./docs/RS485%20压力变送器%20Modbus-RTU%20协议开发手册.md)
- [XDA-10F-100P-7 通信协议](./docs/XDA-10F-100P-7%20四合一数字电极通信协议开发手册.md)

## 说明

- 固件仅接受 FAT32 SD 卡，不会在设备端自动格式化。
- 当前未接入有效 RTC，快照和日志时间使用 `HAL_GetTick()`。
- 当前默认使用自动收发 RS485 模块；若硬件需要手动控制 `DE/RE`，需补充方向控制逻辑。
- CubeMX 生成代码和第三方驱动的版权与许可，以源文件头及 `Drivers/` 中的许可文件为准。
