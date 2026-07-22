# Blue-Eye 二进制传感器日志格式 v2

本文描述固件写入 `0:/SENSOR.BIN` 的 v2 线格式。所有多字节整数均采用小端序；有符号整数采用二进制补码；浮点数采用 IEEE-754 单精度格式并按其 32 位位模式以小端序存储。

## 文件布局

| 偏移 | 长度 | 内容 |
| ---: | ---: | --- |
| `0` | `512` | 日志头副本 A |
| `512` | `512` | 日志头副本 B |
| `1024` | `capacity_records * 64` | 固定槽位的环形记录区 |

日志头 A/B 交替写入。启动时，固件分别验证两份日志头的魔数、版本、尺寸、边界和 CRC32，并选择有效且 `generation` 更新的一份。这样，一次日志头更新过程中掉电时通常仍能从另一份有效副本恢复。

## 512 字节日志头

| 偏移 | 长度 | 类型 | 字段 | 说明 |
| ---: | ---: | --- | --- | --- |
| `0` | 4 | `uint32` | `magic` | `0x32455945`，字节为 `45 59 45 32`（`EYE2`） |
| `4` | 2 | `uint16` | `version` | 当前为 `2` |
| `6` | 2 | `uint16` | `header_size` | 固定为 `512` |
| `8` | 4 | `uint32` | `record_size` | 固定为 `64` |
| `12` | 4 | `uint32` | `capacity_records` | 环形区记录容量 |
| `16` | 4 | `uint32` | `next_write_index` | 下一条记录要写入的槽位 |
| `20` | 4 | `uint32` | `record_count` | 当前有效记录数，不大于容量 |
| `24` | 4 | `uint32` | `overwrite_count` | 环形区写满后发生的覆盖次数 |
| `28` | 4 | `uint32` | `generation` | 每次提交日志头时递增 |
| `32` | 476 | - | `reserved` | 当前写 0，解析器应忽略 |
| `508` | 4 | `uint32` | `crc32` | 对字节 `0..507` 计算的 CRC32 |

当 `record_count < capacity_records` 时，有效记录位于槽位 `0 .. record_count-1`。当环形区已满时，`next_write_index` 指向最旧记录，按该索引开始并循环读取 `record_count` 条即可得到从旧到新的时间顺序。

## 64 字节传感器记录

| 偏移 | 长度 | 类型 | 字段 | 说明 |
| ---: | ---: | --- | --- | --- |
| `0` | 4 | `uint32` | `magic` | `0x52434542`，字节为 `42 45 43 52`（`BECR`） |
| `4` | 2 | `uint16` | `version` | 当前为 `2` |
| `6` | 2 | `uint16` | `record_size` | 固定为 `64` |
| `8` | 4 | `uint32` | `sequence` | 5 秒快照序号，上电后从 1 递增 |
| `12` | 4 | `uint32` | `tick_ms` | 捕获快照时的 `HAL_GetTick()` |
| `16` | 4 | `uint32` | `pressure_sample_tick` | 压力传感器最后一次成功更新时刻 |
| `20` | 4 | `uint32` | `pressure_sample_sequence` | 压力传感器成功样本序号 |
| `24` | 4 | `uint32` | `xda_sample_tick` | XDA 最后一次成功更新时刻 |
| `28` | 4 | `uint32` | `xda_sample_sequence` | XDA 成功样本序号 |
| `32` | 4 | `float32` | `pressure_value` | FLOAT 模式压力值；是否有效由 `pressure_float_valid` 指示 |
| `36` | 2 | `int16` | `pressure_raw` | RAW 模式原始值 |
| `38` | 2 | `uint16` | `pressure_unit_code` | 压力单位代码 |
| `40` | 2 | `uint16` | `pressure_decimal_point` | RAW 模式小数位数 |
| `42` | 2 | `uint16` | `ec_x100` | 电导率乘 100 |
| `44` | 2 | `int16` | `temperature_x10` | 温度乘 10 |
| `46` | 2 | `uint16` | `tds_ppm` | TDS |
| `48` | 2 | `uint16` | `salinity_ppm` | 盐度 |
| `50` | 1 | `uint8` | `pressure_online` | 压力传感器在线标记 |
| `51` | 1 | `uint8` | `pressure_status` | 压力传感器状态枚举 |
| `52` | 1 | `uint8` | `pressure_float_valid` | `pressure_value` 有效标记 |
| `53` | 1 | `uint8` | `pressure_read_mode` | `0=RAW`，`1=FLOAT` |
| `54` | 1 | `uint8` | `xda_online` | XDA 在线标记 |
| `55` | 1 | `uint8` | `xda_status` | XDA 状态枚举 |
| `56` | 1 | `uint8` | `pressure_exception_code` | 最近一次压力 Modbus 异常码，无异常为 0 |
| `57` | 1 | `uint8` | `xda_exception_code` | 最近一次 XDA Modbus 异常码，无异常为 0 |
| `58` | 2 | - | `reserved` | 当前写 0，解析器应忽略 |
| `60` | 4 | `uint32` | `crc32` | 对字节 `0..59` 计算的 CRC32 |

传感器状态值在两个模块中采用相同顺序：`0=IDLE`、`1=OK`、`2=TIMEOUT`、`3=CRC_ERROR`、`4=FRAME_ERROR`、`5=UART_ERROR`、`6=MODBUS_EXCEPTION`。

## CRC32 参数

- 初始值：`0xFFFFFFFF`
- 反射多项式：`0xEDB88320`
- 输入和输出按最低位优先处理
- 最终异或：`0xFFFFFFFF`

该参数组合与常见的 CRC-32/ISO-HDLC（也常称 CRC-32/ADCCP）一致。

## 兼容与数据保护

- 固件只创建和写入 v2 文件。
- 非空但无法识别的 `SENSOR.BIN` 会原样保留，固件不会自动截断、迁移或格式化 SD 卡。
- 升级前如卡中已有旧版日志，应先在电脑上备份，然后将旧文件改名或删除；下次启动写日志时固件才会创建新的 v2 文件。
- 解析器应验证头和每条记录的 CRC，并忽略保留字段，以便未来兼容扩展。
