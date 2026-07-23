from __future__ import annotations

import argparse
import threading
import webbrowser

import uvicorn


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Blue Eye USART1 telemetry viewer")
    parser.add_argument("--host", default="127.0.0.1", help="HTTP listen address")
    parser.add_argument("--port", type=int, default=8765, help="HTTP listen port")
    parser.add_argument(
        "--no-browser",
        action="store_true",
        help="do not open the dashboard in the default browser",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    url = f"http://{args.host}:{args.port}"
    if not args.no_browser:
        threading.Timer(0.8, lambda: webbrowser.open(url)).start()
    uvicorn.run(
        "telemetry_viewer.app:app",
        host=args.host,
        port=args.port,
        log_level="info",
    )


if __name__ == "__main__":
    main()

