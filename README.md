# Blue-Eye

`Blue-Eye` 是一个基于 `STM32F407VGT6` 的嵌入式固件工程，用于采集两类串口传感器数据，并通过 `OLED` 屏进行本地显示。当前工程由 `STM32CubeMX` 生成底层初始化代码，构建系统使用 `CMake + Ninja + arm-none-eabi-gcc`。

## 项目简介

当前固件主要完成以下工作：

- 通过 `USART3` 轮询采集 `XDA-10F-100P-7` 四合一数字电极数据
- 通过 `USART3` 轮询采集 `RS485` 压力变送器数据
- 通过 `SDIO + FATFS` 每 `5s` 将当前传感器快照写入 `SD` 卡环形日志文件
- 通过 `USART1` 每 `5s` 输出一帧传感器遥测数据
- 通过 `I2C1` 驱动 `SSD1306` 兼容 `128x64` OLED 显示屏
- 通过 `PA0` 按键实现页面切换和功能切换
- 通过 `PB2` 指示灯反映当前界面状态

## 功能特性

- 双传感器轮询采集，由单一 `ModbusBus` 模块独占并仲裁共享的 `USART3`
- `SD` 卡环形日志存储，空间用满后覆盖最早记录
- `USART1` 周期遥测输出，便于上位机定时采集
- 每个 5 秒事件只生成一份带采样序号、采样时间和状态的数据快照，由 SD 日志与遥测共同消费
- `USART1` 遥测发送采用异步 `DMA TX + 中断完成`，繁忙时保留最新待发快照，不在主循环中等待
- `USART3` 传感器总线采用完整非阻塞状态机，使用 `TX IT -> TX 完成中断立即启动 Receive-to-IDLE DMA -> 超时/CRC/异常帧处理` 推进 Modbus 访问
- `SDIO + FATFS` 读写链路已切换到 `sd_diskio.c` 的 DMA 模板风格实现
- 日志任务使用 8 条 RAM 队列吸收短时写卡延迟，并仅在 Modbus 总线空闲时访问 FatFs
- 日志文件采用双 512 字节带 CRC 的元数据头和固定 64 字节记录，提高掉电后恢复能力
- 固件不会自动格式化 SD 卡，也不会覆盖无法识别的旧版或损坏日志文件
- OLED 多页面显示，展示实时数据和通信状态
- 按键单击切页、长按执行页面附加功能
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
| 遥测串口 | `USART1` | `9600 8N1` |
| 调试串口 | `USART2` | `9600 8N1` |
| 传感器总线 | `USART3` | `9600 8N1` |
| XDA 传感器 | `USART3` | 从机地址 `0x02` |
| 压力传感器 | `USART3` | 从机地址 `0x01` |

说明：

- `XDA` 代码当前按固定地址和固定波特率工作，不做自动扫描
- 压力传感器地址 `0x01` 是代码默认值，是否与现场设备一致需要联调确认
- 两个传感器当前共用 `USART3`，需要保证总线拓扑、从机地址和轮询时序不冲突
- `USART1` 用于向外部主机输出周期遥测数据，当前协议为自定义 ASCII 帧

## 页面与交互

- 上电后进入欢迎页
- 按键单击：`欢迎页 -> XDA 页 -> 压力页 -> SD CONFIG -> UART CONFIG -> 欢迎页 ...` 循环切换
- 按键长按：在当前功能页执行附加功能；当前在压力页切换 `RAW` / `FLOAT` 采集显示模式，在 `SD CONFIG` 页切换日志写入开关，在 `UART CONFIG` 页切换 `USART1` 周期输出开关
- 欢迎页时 `PB2` 熄灭
- 数据页面时 `PB2` 点亮

显示内容包括：

- `XDA` 页面：`EC`、温度、`TDS`、盐度、从机地址、通信状态
- 压力页面：压力值、单位、模式信息、原始值或寄存器来源、从机地址、通信状态
- `SD CONFIG` 页面：日志开关状态、插卡状态、记录数或安全拔卡提示
- `UART CONFIG` 页面：`USART1` 周期输出开关状态和发送计数

## 引脚速览

| 功能 | 引脚 |
| --- | --- |
| 按键 `KEY` | `PA0` |
| 遥测串口 `USART1_TX/RX` | `PA9` / `PA10` |
| 调试串口 `USART2_TX/RX` | `PA2` / `PA3` |
| 状态 LED | `PB2` |
| OLED `I2C1_SCL/SDA` | `PB6` / `PB7` |
| 传感器串口 `USART3_TX/RX` | `PD8` / `PD9` |
| `SDIO_D0~D3/CK/CMD` | `PC8`~`PC12` / `PD2` |
| SD 卡检测（低电平表示插卡） | `PD3` |

更完整的板级说明见：

- [docs/board_pin_config.md](./docs/board_pin_config.md)

## 软件架构

工程主要由两部分组成：

- `STM32CubeMX` 生成的底层外设初始化代码
- 用户自定义的传感器、按键、OLED 和 UI 逻辑

关键模块如下：

- `Core/Src/main.c`：主循环、外设初始化、任务调度入口
- `Core/Src/modbus_bus.c`：共享 `USART3` 的唯一所有者，负责请求构造、总线时序、CRC、超时和结果分发
- `Core/Src/app_ui.c`：OLED 页面渲染和页面切换逻辑
- `Core/Src/data_logger.c`：快照队列、SD 卡挂载、双头环形日志文件维护和错误恢复
- `Core/Src/key.c`：按键去抖、单击/长按识别
- `Core/Src/oled.c`：OLED 驱动
- `Core/Src/xda_sensor.c`：XDA 传感器轮询策略、结果校验、在线状态和样本新鲜度维护
- `Core/Src/pressure_sensor.c`：压力传感器 RAW/FLOAT 多阶段轮询、结果校验和样本状态维护
- `Core/Src/telemetry_uart.c`：USART1 遥测帧打包、异步发送、超时和待发快照合并
- `Core/Src/sensor_record.c`：日志与遥测共用的不可变传感器快照及 64 字节线格式序列化
- `Core/Src/uart_dma_support.c`：UART DMA/中断收发状态与 HAL 完成回调桥接
- `Core/Src/periodic_trigger.c`：中断安全的 1 秒计数和 5 秒待处理事件队列

主循环按协作式状态机运行：

1. 初始化 GPIO、DMA、I2C、USART、SDIO
2. 初始化 OLED、Modbus 总线、两个传感器、日志、遥测和 UI
3. 推进 Modbus 总线及两个传感器状态机；只有数据或状态真正变化时才刷新对应页面
4. 处理按键事件和配置页面
5. 消费 `TIM6` 累计出的 5 秒事件，构造一次统一快照并提交给日志队列与遥测队列
6. 推进异步 USART1 发送；仅在 Modbus 总线空闲时执行同步 FatFs 操作
7. 无任务可立即推进时执行 `__WFI()`，等待下一次中断唤醒

## SD 卡日志

- 日志文件固定为 `0:/SENSOR.BIN`
- 文件系统必须预先在电脑上格式化为本工程 FatFs 配置支持的 FAT 文件系统；固件检测到无文件系统时只显示 `FORMAT`，不会自动格式化或删除数据
- 如果 `SENSOR.BIN` 不存在，固件会创建 v2 日志；如果存在非空但日志头无效或属于旧版本，固件会保留原文件并停止写入，等待人工备份、改名或删除
- 每条记录固定为 `64` 字节，包含快照序号、快照时刻、两类传感器各自的最后成功采样时刻与样本序号、测量值、状态、Modbus 异常码和记录 CRC32
- 文件开头有两份交替更新的 `512` 字节日志头，每份均带 CRC32；上电时选择有效且代数更新的一份恢复环形索引
- 日志文件采用固定容量环形结构，写满后从最早记录位置开始覆盖
- 新文件容量根据 SD 卡空闲空间计算，预留约 `64 KiB`，并限制为最多 `262144` 条记录（约 `16 MiB`；5 秒周期下约 `15.17` 天）
- 5 秒事件到达时立即把快照复制到 8 条 RAM 队列；FatFs 写入延迟不会改变已经捕获的快照内容，队列满时累计丢弃计数
- FatFs 的磁盘接口仍是同步接口，`sd_diskio.c` 内会等待 DMA 完成；调度器只在 `USART3` Modbus 总线空闲时调用日志任务，因此写卡不会卡住正在进行的传感器应答接收
- SDIO DMA 软件等待使用独立的 `1000 ms` 毫秒超时；超时或检测到拔卡时会中止当前 HAL SD/DMA 传输并把磁盘标记为需要重新初始化，避免主循环永久停在写卡等待中
- `SD CONFIG` 页面关闭日志后会先关闭文件并卸载文件系统；页面显示 `REMOVE` 后才允许安全拔卡
- 在 `SD CONFIG` 页面长按按键可切换日志写入开关，避免写卡过程中直接插拔 `SD` 卡
- `sd_diskio.c` 已按 CubeMX DMA 模板思路改写，使用 `BSP_SD_ReadBlocks_DMA()/WriteBlocks_DMA()`、完成回调和未对齐缓冲处理
- 当前 `SDIO` DMA 读写依赖 `DMA2_Stream6 (RX)`、`DMA2_Stream3 (TX)`、`SDIO_IRQn` 与对应 DMA 中断共同完成
- 完整字节布局见 [sensor_log_format.md](./docs/sensor_log_format.md)

## USART1 遥测协议

- 发送周期：`5s`
- 接口参数：`USART1`，`9600 8N1`
- 数据格式：ASCII 帧，帧头为 `$`，帧尾为 `\r\n`
- 校验方式：`*` 前的 2 位十六进制异或校验
- 遥测发送与 `SD` 日志共用同一个 5 秒硬件定时事件和同一份传感器快照
- 新增 `UART CONFIG` 页面；在该页面长按可单独开启或关闭 `USART1` 的 5 秒周期输出
- 发送底层使用 CubeMX 已配置的 `USART1_TX DMA` 通道，实际发送通过 `HAL_UART_Transmit_DMA()` 异步完成；发送繁忙时以最新快照覆盖旧的待发快照并累计合并计数
- 字段定义：
  - `tick_ms`：系统运行毫秒计数
  - `pressure_online/status/read_mode/float_valid/raw/value_x1000/unit/decimal_point`
  - `xda_online/status/ec_x100/temperature_x10/tds_ppm/salinity_ppm`
  - `sd_enabled/log_record_count/log_overwrite_count`
- 典型报文示例：

```text
$BE,15000,1,1,0,0,1234,-2147483648,0,2,1,1,256,253,400,12,1,3,0*63
```

## 开发环境

建议准备以下工具：

- VS Code
- [STM32CubeIDE for Visual Studio Code 插件](https://marketplace.visualstudio.com/items?itemName=stmicroelectronics.stm32-vscode-extension)
- 可选：`STM32CubeMX` 或 `STM32CubeIDE`，用于重新生成 `.ioc` 对应的底层代码

## CubeMX 配置指南

- **USART1 遥测**
- `Pinout & Configuration -> USART1 -> Mode` 选择 `Asynchronous`
- `Parameter Settings` 设为 `9600 8N1`
- `DMA Settings` 新增 `USART1_TX`，推荐 `DMA2_Stream7 / Channel4 / Normal / Memory Increment`
- `NVIC Settings` 使能 `USART1 global interrupt`

- **USART3 传感器总线**
- `Pinout & Configuration -> USART3 -> Mode` 选择 `Asynchronous`
- `Parameter Settings` 设为 `9600 8N1`
- `DMA Settings` 新增 `USART3_RX`，推荐 `DMA1_Stream1 / Channel4 / Normal / Memory Increment`
- `NVIC Settings` 使能 `USART3 global interrupt`
- 保留 `DMA1_Stream1_IRQn` 中断使能，因为完整非阻塞状态机依赖 `RX DMA` 完成回调

- **SDIO + FatFs**
- `Pinout & Configuration -> SDIO` 选择 `4 bits Wide bus`
- `DMA Settings` 同时新增：
- `SDIO_RX`：推荐 `DMA2_Stream6 / Channel4 / Peripheral flow controller / Word alignment / FIFO enable / INC4 burst`
- `SDIO_TX`：推荐 `DMA2_Stream3 / Channel4 / Peripheral flow controller / Word alignment / FIFO enable / INC4 burst`
- `NVIC Settings` 使能 `SDIO_IRQn`、`DMA2_Stream6_IRQn`、`DMA2_Stream3_IRQn`
- `Middleware -> FATFS` 绑定 `SDIO`
- 重新生成后，确认 `sdio.c` 中同时存在 `hdma_sdio_rx` 与 `hdma_sdio_tx`

- **TIM6 五秒节拍**
- `Pinout & Configuration -> TIM6` 选择 `Internal Clock`
- `Parameter Settings` 按当前时钟配置设置为 `Prescaler=15999`、`Period=999`
- `NVIC Settings` 使能 `TIM6 global interrupt`
- 当前代码按 `1s` 中断累计 5 次生成一个 5 秒事件

- **EXTI 与其它 DMA**
- `PA0` 作为 `GPIO_EXTI0`，保持双边沿中断
- `USART2_RX DMA` 可继续保留用于调试或后续扩展

- **NVIC 优先级**

| 中断 | 抢占优先级 | 说明 |
| --- | ---: | --- |
| `USART3_IRQn`、`DMA1_Stream1_IRQn` | 2 | 传感器 Modbus 收发，最高业务优先级 |
| `SDIO_IRQn`、`DMA2_Stream3/6_IRQn` | 4 | SDIO 数据搬运与完成处理 |
| `USART1_IRQn`、`DMA2_Stream7_IRQn` | 6 | 遥测发送 |
| `TIM6_DAC_IRQn` | 8 | 周期节拍 |
| `EXTI0_IRQn`、`USART2_IRQn`、`DMA1_Stream5_IRQn` | 10 | 按键与保留调试通道 |
| `SysTick_IRQn` | 15 | HAL 毫秒时基 |

优先级分组使用 `NVIC_PRIORITYGROUP_4`。请保持源码和 `blue-eye.ioc` 一致，避免 CubeMX 再生成后退回全部优先级为 0。

- **I2C1 / OLED 是否需要 DMA**
- 当前不建议为 `I2C1` 打开 DMA
- 原因是 OLED 刷新只在页面更新时触发，带宽和频率都很低，DMA 收益有限
- 若引入 `I2C DMA`，需要额外处理命令/数据分包、DMA 完成同步和 OLED 刷新忙状态，复杂度高于收益
- 因此当前保留 `HAL_I2C_Master_Transmit()` 的阻塞实现，更适合后期维护

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
- [sensor_log_format.md](./docs/sensor_log_format.md)
- [RS485 压力变送器 Modbus-RTU 协议开发手册.md](./docs/RS485%20压力变送器%20Modbus-RTU%20协议开发手册.md)
- [XDA-10F-100P-7 四合一数字电极通信协议开发手册.md](./docs/XDA-10F-100P-7%20四合一数字电极通信协议开发手册.md)

## License

`STM32CubeMX` 生成代码与第三方驱动的版权和许可说明，请以各源文件头注释及 `Drivers/` 目录中的许可文件为准。
