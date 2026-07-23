# Blue-Eye SD 传感器日志格式 v1

当前固件在 FAT32 SD 卡的 `0:/LOG` 目录中创建顺序日志文件：

```text
LOG00000.BIN
LOG00001.BIN
LOG00002.BIN
...
```

每次上电、重新插卡或从安全卸载状态恢复时都会创建新的文件。单个文件达到约 `32 MiB` 后自动创建下一个文件，不覆盖已有文件。

所有多字节整数采用小端序。有符号整数使用二进制补码，`float32` 使用 IEEE-754 单精度格式。

## 文件布局

| 偏移 | 长度 | 内容 |
| ---: | ---: | --- |
| `0` | `512` | 文件头 |
| `512` | `N * 64` | 固定长度传感器记录 |

文件没有必须存在的尾部结构。异常断电或直接拔卡后，解析器应从文件头之后按 64 字节读取，并在遇到长度不足、魔数错误或 CRC 错误的记录时停止。

## 512 字节文件头

| 偏移 | 长度 | 类型 | 字段 | 说明 |
| ---: | ---: | --- | --- | --- |
| `0` | 8 | 字节数组 | `magic` | ASCII `BEYELOG1` |
| `8` | 2 | `uint16` | `file_version` | 当前为 `1` |
| `10` | 2 | `uint16` | `header_size` | 固定为 `512` |
| `12` | 2 | `uint16` | `record_size` | 固定为 `64` |
| `14` | 2 | `uint16` | `record_version` | 当前为 `3` |
| `16` | 4 | `uint32` | `sample_period_ms` | 当前为 `5000` |
| `20` | 4 | `uint32` | `file_index` | 文件名中的五位序号 |
| `24` | 4 | `uint32` | `start_tick_ms` | 创建文件时的 `HAL_GetTick()` |
| `28` | 480 | - | `reserved` | 当前写 0，解析器必须忽略 |
| `508` | 4 | `uint32` | `crc32` | 对文件头字节 `0..507` 计算 |

当前硬件工程尚未接入有效 RTC，因此文件头和记录使用系统运行毫秒计数，不包含日历时间。预留区可用于以后增加 UTC 时间、设备编号和固件版本，而不改变 512 字节文件头大小。

## 64 字节传感器记录 v3

| 偏移 | 长度 | 类型 | 字段 | 说明 |
| ---: | ---: | --- | --- | --- |
| `0` | 4 | `uint32` | `magic` | `0x52434542`，文件字节为 `42 45 43 52` (`BECR`) |
| `4` | 2 | `uint16` | `version` | 当前为 `3` |
| `6` | 2 | `uint16` | `record_size` | 固定为 `64` |
| `8` | 4 | `uint32` | `sequence` | 周期快照序号；错过周期时会跳号 |
| `12` | 4 | `uint32` | `tick_ms` | 构建快照时的系统毫秒计数 |
| `16` | 4 | `uint32` | `pressure_sample_tick` | 压力传感器最近成功更新时间 |
| `20` | 4 | `uint32` | `pressure_sample_sequence` | 压力传感器样本序号 |
| `24` | 4 | `uint32` | `xda_sample_tick` | XDA 最近成功更新时间 |
| `28` | 4 | `uint32` | `xda_sample_sequence` | XDA 样本序号 |
| `32` | 4 | `float32` | `pressure_value` | FLOAT 模式压力值 |
| `36` | 2 | `int16` | `pressure_raw` | RAW 模式压力值 |
| `38` | 2 | `uint16` | `pressure_unit_code` | 压力单位代码 |
| `40` | 2 | `uint16` | `pressure_decimal_point` | RAW 模式小数位数 |
| `42` | 2 | `uint16` | `ec_x100` | 电导率乘 100 |
| `44` | 2 | `int16` | `temperature_x10` | 温度乘 10 |
| `46` | 2 | `uint16` | `tds_ppm` | TDS |
| `48` | 2 | `uint16` | `salinity_ppm` | 盐度 |
| `50` | 1 | `uint8` | `pressure_online` | 压力传感器在线标志 |
| `51` | 1 | `uint8` | `pressure_status` | 压力传感器状态 |
| `52` | 1 | `uint8` | `pressure_float_valid` | 浮点压力有效标志 |
| `53` | 1 | `uint8` | `pressure_read_mode` | `0=RAW`，`1=FLOAT` |
| `54` | 1 | `uint8` | `xda_online` | XDA 在线标志 |
| `55` | 1 | `uint8` | `xda_status` | XDA 状态 |
| `56` | 1 | `uint8` | `pressure_exception_code` | 最近压力 Modbus 异常码 |
| `57` | 1 | `uint8` | `xda_exception_code` | 最近 XDA Modbus 异常码 |
| `58` | 2 | - | `reserved` | 当前写 0 |
| `60` | 4 | `uint32` | `crc32` | 对记录字节 `0..59` 计算 |

传感器状态值均按各模块的状态枚举保存：

```text
0 IDLE
1 OK
2 TIMEOUT
3 CRC_ERROR
4 FRAME_ERROR
5 UART_ERROR
6 MODBUS_EXCEPTION
```

## CRC32 参数

- 初始值：`0xFFFFFFFF`
- 反射多项式：`0xEDB88320`
- 按最低位优先处理
- 最终异或：`0xFFFFFFFF`

该参数组合为常见的 CRC-32/ISO-HDLC。

## 写入与同步策略

- RAM 队列容量为 128 条记录。
- 正常情况下每 8 条记录组成 512 字节后写入 SD 卡，约每 40 秒一次。
- 有新数据写入时，最长约每 120 秒执行一次 `f_sync()`。
- 安全卸载时停止接收新记录，排空全部队列，写出不足 8 条的最后一批数据，然后同步、关闭文件并卸载文件系统。
- 队列满时覆盖最旧的未写记录，并累计丢弃计数。
- 不支持在设备上自动格式化、删除或覆盖已有日志。

## 安全卸载

在 OLED 的 `SD LOGGER` 页面持续按键 3 秒会进入 `FLUSHING` 状态。只有屏幕显示 `SAFE TO REMOVE` 后才允许拔卡。

安全卸载完成后固件保持 SD 卡未挂载，不会因为卡仍插在卡槽中而自动重新开始写入。拔出并重新插入会自动创建新文件；在 `SAFE TO REMOVE` 状态再次长按 3 秒也可以重新挂载并恢复记录。遇到非格式类 `ERROR` 时也可长按 3 秒，强制重新执行 SD 初始化和挂载。
