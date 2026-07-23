from __future__ import annotations

import asyncio
import csv
import io
import json
import threading
from pathlib import Path
from typing import Any, Iterator

from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import FileResponse, PlainTextResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from .parser import CSV_COLUMNS, Dataset, DatasetLoader
from .system_paths import browse_directory, list_drives


STATIC_DIR = Path(__file__).resolve().parent / "static"


class LoadRequest(BaseModel):
    path: str = Field(min_length=1, max_length=4096)


class DatasetState:
    def __init__(self) -> None:
        self._lock = threading.RLock()
        self._dataset: Dataset | None = None

    def set(self, dataset: Dataset) -> None:
        with self._lock:
            self._dataset = dataset

    def get(self) -> Dataset | None:
        with self._lock:
            return self._dataset

    def require(self) -> Dataset:
        dataset = self.get()
        if dataset is None:
            raise LookupError("尚未读取日志，请先选择 SD 卡根目录、LOG 目录或 BIN 文件")
        return dataset


loader = DatasetLoader()
state = DatasetState()

app = FastAPI(title="Blue Eye SD Log Reader", version="0.1.0")
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


@app.get("/api/drives")
async def drives() -> dict[str, Any]:
    try:
        return {"drives": await asyncio.to_thread(list_drives)}
    except OSError as exc:
        raise HTTPException(status_code=503, detail=f"无法枚举磁盘：{exc}") from exc


@app.get("/api/browse")
async def browse(path: str = Query(min_length=1, max_length=4096)) -> dict[str, Any]:
    try:
        return await asyncio.to_thread(browse_directory, path)
    except (FileNotFoundError, ValueError, PermissionError, OSError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc


@app.get("/api/snapshot")
async def snapshot(include_preview: bool = True) -> dict[str, Any]:
    dataset = state.get()
    if dataset is None:
        return {"loaded": False, "summary": None, "files": [], "preview": []}
    return dataset.to_dict(include_preview=include_preview)


@app.post("/api/load")
async def load_logs(request: LoadRequest) -> dict[str, Any]:
    try:
        dataset = await asyncio.to_thread(loader.load, request.path)
    except (FileNotFoundError, ValueError, PermissionError) as exc:
        raise HTTPException(status_code=400, detail=str(exc)) from exc
    except OSError as exc:
        raise HTTPException(status_code=503, detail=f"读取日志失败：{exc}") from exc
    state.set(dataset)
    return dataset.to_dict(include_preview=True)


@app.get("/api/records")
async def records(
    offset: int = Query(default=0, ge=0),
    limit: int = Query(default=100, ge=1, le=1000),
) -> dict[str, Any]:
    try:
        dataset = state.require()
        selected = await asyncio.to_thread(
            lambda: [record.to_dict() for record in dataset.iter_records(offset, limit)]
        )
    except LookupError as exc:
        raise HTTPException(status_code=409, detail=str(exc)) from exc
    except OSError as exc:
        raise HTTPException(status_code=503, detail=f"日志文件已不可用或内容已变化：{exc}") from exc
    return {
        "offset": offset,
        "limit": limit,
        "total": dataset.total_records,
        "records": selected,
    }


def _csv_stream(dataset: Dataset) -> Iterator[str]:
    buffer = io.StringIO()
    writer = csv.DictWriter(buffer, fieldnames=CSV_COLUMNS, extrasaction="ignore", lineterminator="\r\n")
    buffer.write("\ufeff")
    writer.writeheader()
    yield buffer.getvalue()
    buffer.seek(0)
    buffer.truncate(0)

    for index, record in enumerate(dataset.iter_records()):
        writer.writerow(record.to_dict())
        if index % 256 == 255:
            yield buffer.getvalue()
            buffer.seek(0)
            buffer.truncate(0)
    remainder = buffer.getvalue()
    if remainder:
        yield remainder


@app.get("/api/export.csv")
async def export_csv() -> StreamingResponse:
    try:
        dataset = state.require()
    except LookupError as exc:
        raise HTTPException(status_code=409, detail=str(exc)) from exc
    return StreamingResponse(
        _csv_stream(dataset),
        media_type="text/csv; charset=utf-8",
        headers={"Content-Disposition": 'attachment; filename="blue-eye-sd-log.csv"'},
    )


def _json_stream(dataset: Dataset) -> Iterator[str]:
    yield '{"summary":'
    yield json.dumps(dataset.summary_dict(), ensure_ascii=False, allow_nan=False)
    yield ',"files":'
    yield json.dumps([file.to_dict() for file in dataset.files], ensure_ascii=False, allow_nan=False)
    yield ',"records":['
    first = True
    for record in dataset.iter_records():
        if not first:
            yield ","
        first = False
        yield json.dumps(record.to_dict(), ensure_ascii=False, allow_nan=False, separators=(",", ":"))
    yield "]}"


@app.get("/api/export.json")
async def export_json() -> StreamingResponse:
    try:
        dataset = state.require()
    except LookupError as exc:
        raise HTTPException(status_code=409, detail=str(exc)) from exc
    return StreamingResponse(
        _json_stream(dataset),
        media_type="application/json; charset=utf-8",
        headers={"Content-Disposition": 'attachment; filename="blue-eye-sd-log.json"'},
    )
