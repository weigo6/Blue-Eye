from __future__ import annotations

import asyncio
import json
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any, Literal

from fastapi import FastAPI, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse, PlainTextResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .serial_service import TelemetryMonitor, list_serial_ports


STATIC_DIR = Path(__file__).resolve().parent / "static"


class WebSocketHub:
    def __init__(self) -> None:
        self.clients: set[WebSocket] = set()
        self.lock = asyncio.Lock()

    async def add(self, websocket: WebSocket) -> None:
        await websocket.accept()
        async with self.lock:
            self.clients.add(websocket)

    async def remove(self, websocket: WebSocket) -> None:
        async with self.lock:
            self.clients.discard(websocket)

    async def broadcast(self, message: dict[str, Any]) -> None:
        async with self.lock:
            clients = list(self.clients)
        stale: list[WebSocket] = []
        for client in clients:
            try:
                await client.send_json(message)
            except Exception:
                stale.append(client)
        if stale:
            async with self.lock:
                for client in stale:
                    self.clients.discard(client)


hub = WebSocketHub()
monitor = TelemetryMonitor(hub.broadcast)


class ConnectRequest(BaseModel):
    port: str = Field(min_length=1)
    baud_rate: int = Field(default=9600, ge=300, le=4_000_000)
    data_bits: Literal[5, 6, 7, 8] = 8
    parity: Literal["N", "E", "O"] = "N"
    stop_bits: Literal[1, 1.5, 2] = 1


class DemoRequest(BaseModel):
    period_ms: int = Field(default=1000, ge=100, le=60_000)
    inject_noise: bool = True


@asynccontextmanager
async def lifespan(_: FastAPI):
    yield
    await monitor.disconnect()


app = FastAPI(
    title="Blue Eye Telemetry Viewer",
    version="0.1.0",
    lifespan=lifespan,
)
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


@app.get("/", include_in_schema=False)
async def index() -> FileResponse:
    return FileResponse(STATIC_DIR / "index.html")


@app.get("/favicon.ico", include_in_schema=False)
async def favicon() -> PlainTextResponse:
    return PlainTextResponse("", status_code=204)


@app.get("/api/health")
async def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/api/ports")
async def ports() -> dict[str, Any]:
    return {"ports": await asyncio.to_thread(list_serial_ports)}


@app.get("/api/snapshot")
async def snapshot(record_limit: int = Query(default=150, ge=0, le=1000)) -> dict[str, Any]:
    return monitor.snapshot(record_limit=record_limit)


@app.post("/api/connect")
async def connect(request: ConnectRequest) -> dict[str, Any]:
    try:
        await monitor.connect(**request.model_dump())
    except (ValueError, KeyError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    except Exception as exc:
        raise HTTPException(status_code=503, detail=f"无法打开串口 {request.port}: {exc}") from exc
    return monitor.snapshot()


@app.post("/api/demo")
async def demo(request: DemoRequest) -> dict[str, Any]:
    await monitor.start_demo(**request.model_dump())
    return monitor.snapshot()


@app.post("/api/disconnect")
async def disconnect() -> dict[str, Any]:
    await monitor.disconnect()
    return monitor.snapshot()


@app.post("/api/clear")
async def clear() -> dict[str, Any]:
    await monitor.clear()
    return monitor.snapshot()


@app.get("/api/records")
async def records(
    limit: int = Query(default=200, ge=1, le=1000),
    kind: Literal["all", "raw", "frame", "noise"] = "all",
) -> dict[str, Any]:
    selected = list(monitor.records)
    if kind != "all":
        selected = [record for record in selected if record["kind"] == kind]
    return {"records": selected[-limit:]}


@app.get("/api/export.json")
async def export_json() -> JSONResponse:
    return JSONResponse(
        content={
            "status": monitor.status(),
            "records": list(monitor.records),
        },
        headers={"Content-Disposition": 'attachment; filename="blue-eye-telemetry.json"'},
    )


@app.get("/api/export.csv")
async def export_csv() -> PlainTextResponse:
    return PlainTextResponse(
        monitor.export_csv(),
        media_type="text/csv; charset=utf-8",
        headers={"Content-Disposition": 'attachment; filename="blue-eye-telemetry.csv"'},
    )


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await hub.add(websocket)
    try:
        await websocket.send_text(json.dumps({"type": "snapshot", "snapshot": monitor.snapshot()}))
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        await hub.remove(websocket)

