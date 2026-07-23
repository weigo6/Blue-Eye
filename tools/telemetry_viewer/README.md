# Blue Eye 遥测数据接口查看器

这是 `blue-eye` 项目的本地 PC 端 USART1 遥测查看器。它针对固件当前输出的：

```text
$BE,<15 个字段>*XX\r\n
```

协议实现，不把 USB、驱动或 Web Serial/Python 串口读取返回的数据块误认为完整帧。

## 主要能力

- 使用 `pyserial` 枚举并连接本机串口，默认 `9600 8N1`、无流控。
- 严格以 `$BE,` 开始、以 `\r\n` 结束进行流式组帧。
- 对 `BE,...` payload 计算 XOR，并同时显示接收校验与本地计算校验。
- 将帧外字节单独标记为 `NOISE`，保留不会丢失信息的 HEX 表示。
- 同时显示驱动每次返回的 `RAW` 数据块，用来观察 USB/驱动如何拆分同一帧。
- 实时显示压力传感器、XDA 四合一电极、设备 Tick、主机接收时间和帧间隔。
- 保存最近的帧、噪声和原始块，可导出 JSON 与遥测帧 CSV。
- 内置演示模式：故意把一条帧拆成多个读取块，并周期性注入 Modbus 字节，验证查看器能够正确重组。
- 所有服务仅监听 `127.0.0.1`，默认不会暴露到局域网。

## 启动

项目不依赖系统全局 Python，使用 `uv` 自动安装 Python 和依赖：

```powershell
cd D:\Projects\stmcu-projects\blue-eye\tools\telemetry_viewer
uv sync --group dev
uv run python -m telemetry_viewer
```

启动后默认打开：

```text
http://127.0.0.1:8765
```

如果不希望自动打开浏览器：

```powershell
uv run python -m telemetry_viewer --no-browser
```

修改本地 HTTP 端口：

```powershell
uv run python -m telemetry_viewer --port 9000
```

## 接线

只接收 USART1 遥测时，建议先只连接两根线：

```text
STM32 PA9 / USART1_TX  →  CH340 RX
STM32 GND              →  CH340 GND
```

请使用 `3.3 V TTL` 电平。当前查看器不需要 CH340 TX，因此排查期间可以暂时不连接 `PA10 / USART1_RX`。

不要将普通 TTL 串口直接接到 RS485 的 A/B 差分端子。

## 页面使用

1. 点击串口旁的刷新按钮，选择 CH340 对应的 COM 口。
2. 保持 `9600`，点击“连接串口”。
3. 正常遥测帧会出现在“遥测帧历史”中。
4. 串口读取块会显示为蓝色 `RAW`。
5. `$BE,` 之前、帧之间或不完整数据会显示为黄色 `NOISE`。
6. 点击任意原始记录可以查看完整 ASCII escaped 和 HEX。
7. 设备帧间隔正常应接近 `5000 ms`。

如果还没有硬件，点击“演示模式”。演示数据周期为 1 秒，并模拟：

- 一帧被驱动拆为 11、21 和剩余字节三个读取块。
- 块之间存在 9 ms 和 24 ms 延迟。
- 每 5 帧注入一次 Modbus 请求和不可打印字节。

这些条件下，页面仍只应生成一条有效遥测帧，同时把 Modbus 字节单独标记为 `NOISE`。

## 当前协议字段

| 序号 | 字段 | 缩放/说明 |
|---:|---|---|
| 1 | `tick_ms` | STM32 `HAL_GetTick()`，毫秒 |
| 2 | `pressure_online` | 0/1 |
| 3 | `pressure_status` | 0 IDLE、1 OK、2 TIMEOUT、3 CRC、4 FRAME、5 UART、6 MODBUS |
| 4 | `pressure_read_mode` | 0 RAW、1 FLOAT |
| 5 | `pressure_float_valid` | 0/1 |
| 6 | `pressure_raw` | 压力原始寄存器值 |
| 7 | `pressure_value_x1000` | Float 压力乘 1000；无效时为 `INT32_MIN` |
| 8 | `pressure_unit_code` | 设备单位代码 |
| 9 | `pressure_decimal_point` | RAW 小数位数 |
| 10 | `xda_online` | 0/1 |
| 11 | `xda_status` | 与压力状态枚举相同 |
| 12 | `ec_x100` | EC 值乘 100 |
| 13 | `temperature_x10` | 摄氏温度乘 10 |
| 14 | `tds_ppm` | ppm |
| 15 | `salinity_ppm` | ppm |

## 本地 API

| 方法 | 路径 | 用途 |
|---|---|---|
| `GET` | `/api/ports` | 枚举串口 |
| `GET` | `/api/snapshot` | 当前状态、最新帧和最近记录 |
| `POST` | `/api/connect` | 连接串口 |
| `POST` | `/api/demo` | 启动演示数据源 |
| `POST` | `/api/disconnect` | 断开 |
| `POST` | `/api/clear` | 清空记录和统计 |
| `GET` | `/api/records` | 查询最近记录 |
| `GET` | `/api/export.csv` | 导出解析后的遥测帧 |
| `GET` | `/api/export.json` | 导出全部记录 |
| `WS` | `/ws` | 实时推送状态和记录 |

FastAPI 的交互式接口文档位于：

```text
http://127.0.0.1:8765/docs
```

## 测试

```powershell
uv run pytest
```

测试覆盖：

- 截图中 `tick=685220`、校验 `0x38` 的已知帧。
- 任意串口分块后的重组。
- 帧前噪声提取。
- 分块恰好截断 `$BE,` 起始标记。
- 错误 XOR 的保留和报告。
- 单次读取包含多帧。
- 基础 HTTP API 与演示模式生命周期。

## 常见问题

### 无法打开 COM 口

Windows 串口通常不能被两个程序同时独占。先在 BaudDance SerialAssistant 或其他串口助手中断开，再由本查看器连接。

### 能看到 RAW，不能看到 FRAME

检查 RAW/NOISE 的 HEX：

- 正常帧应以 `24 42 45 2C`，即 `$BE,` 开始。
- 正常帧应以 `2A XX XX 0D 0A` 结束。
- 如果出现 `02 03 ...` 或 `01 03 ...`，可能接到了 USART3 Modbus 总线。
- 如果一直是随机字节，检查共地、电平、引脚和是否有多个 TX 并联。

### 一帧仍显示多个 RAW

这是预期行为。RAW 表示操作系统/驱动每次返回的块；FRAME 才表示按照协议重组后的完整消息。

