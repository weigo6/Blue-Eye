# Blue Eye SD 日志解析器

这是 `blue-eye` 固件配套的本地只读工具，用来读取 SD 卡 `LOG/LOGxxxxx.BIN` 文件。工具严格按照 `docs/sensor_log_format.md` 中的 v1 文件头和 v3 记录格式解析。

## 功能

- 直接选择 SD 卡根目录、`LOG` 目录或单个 `BIN` 文件。
- 枚举 Windows 本地磁盘并标记包含 `LOG` 目录的磁盘。
- 校验 512 字节文件头魔数、版本、长度和 CRC-32。
- 顺序校验每条 64 字节记录；遇到短记录、魔数、版本、长度或 CRC 错误时按固件格式约定停止该文件的解析。
- 统计序号跳号、缺失记录、重复或倒序记录。
- 显示压力、EC、温度、TDS、盐度的最小值、最大值和平均值。
- 显示压力传感器与 XDA 的在线状态、错误状态、样本年龄和 Modbus 异常码。
- 对全量有效记录生成下采样趋势预览。
- 分页查看逐条记录，并流式导出 CSV 或 JSON。
- 扫描阶段不在内存中保存全部记录；分页和导出按需重新读取源文件，适合固件生成的 32 MiB 日志文件。

工具仅监听 `127.0.0.1`，并且只读取源文件，不会格式化、删除或修改 SD 卡。

## 启动

使用 `uv` 管理 Python 和依赖：

```powershell
cd D:\Projects\stmcu-projects\blue-eye\tools\sd_log_reader
uv sync --group dev
uv run python -m sd_log_reader
```

浏览器会自动打开：

```text
http://127.0.0.1:8766
```

也可以在启动时直接指定 SD 卡或日志路径：

```powershell
uv run python -m sd_log_reader E:\
uv run python -m sd_log_reader E:\LOG
uv run python -m sd_log_reader E:\LOG\LOG00042.BIN
```

不自动打开浏览器：

```powershell
uv run python -m sd_log_reader --no-browser
```

指定端口：

```powershell
uv run python -m sd_log_reader --port 9001
```

## 使用方式

1. 将 SD 卡通过读卡器连接到电脑。
2. 点击页面顶部的磁盘卡片，或输入盘符、`LOG` 目录、单个日志文件路径。
3. 点击“读取并校验”。
4. 先在“文件完整性”中检查 CRC 或截断问题，再查看趋势与逐条记录。
5. 需要后处理时导出 CSV；需要保留完整字段、文件头和问题信息时导出 JSON。

页面中的“工程压力值”按固件读取模式计算：

- FLOAT 模式且浮点值有效：使用 `pressure_value`。
- RAW 模式：使用 `pressure_raw / 10^pressure_decimal_point`。
- 无法形成有效工程值时显示为空，但原始字段仍保留在记录详情和导出结果中。

## 解析停止规则

每个文件独立解析。文件头无效时，该文件不会继续读取记录。文件头有效后，从偏移 512 开始按 64 字节读取，一旦遇到以下情况就停止当前文件：

- 文件尾不足 64 字节；
- 记录魔数不是 `BECR`；
- 记录版本不是 3；
- 记录声明长度不是 64；
- 记录 CRC-32 不匹配。

其他日志文件仍会继续扫描，因此一张卡中单个损坏文件不会阻止查看其余正常文件。

## API

| 方法 | 路径 | 用途 |
|---|---|---|
| `GET` | `/api/drives` | 枚举本机磁盘 |
| `GET` | `/api/browse?path=...` | 浏览目录和日志文件 |
| `POST` | `/api/load` | 读取路径并扫描日志 |
| `GET` | `/api/snapshot` | 获取汇总、文件结果和趋势预览 |
| `GET` | `/api/records?offset=0&limit=100` | 分页读取有效记录 |
| `GET` | `/api/export.csv` | 流式导出 CSV |
| `GET` | `/api/export.json` | 流式导出 JSON |

FastAPI 交互文档位于 `http://127.0.0.1:8766/docs`。

## 测试

```powershell
uv run pytest
```

测试会动态构造与固件一致的文件头和记录，覆盖正常解析、有符号值、浮点压力、CRC 损坏、尾部截断、序号跳号、多文件排序、分页和导出接口。
