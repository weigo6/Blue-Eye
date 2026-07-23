from __future__ import annotations

import argparse
import threading
import webbrowser
from urllib.parse import quote

import uvicorn


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Blue Eye SD 卡二进制日志读取解析器")
    parser.add_argument("path", nargs="?", help="启动后自动读取的 SD 卡根目录、LOG 目录或 BIN 文件")
    parser.add_argument("--host", default="127.0.0.1", help="HTTP 监听地址")
    parser.add_argument("--port", type=int, default=8766, help="HTTP 监听端口")
    parser.add_argument("--no-browser", action="store_true", help="不自动打开浏览器")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    query = f"?source={quote(args.path)}" if args.path else ""
    url = f"http://{args.host}:{args.port}/{query}"
    if not args.no_browser:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    uvicorn.run(
        "sd_log_reader.app:app",
        host=args.host,
        port=args.port,
        log_level="info",
    )


if __name__ == "__main__":
    main()
