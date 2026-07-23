from __future__ import annotations

import ctypes
import os
from pathlib import Path
from typing import Any

from .parser import LOG_FILE_PATTERN


DRIVE_TYPES = {
    0: "unknown",
    1: "invalid",
    2: "removable",
    3: "fixed",
    4: "network",
    5: "cdrom",
    6: "ramdisk",
}


def list_drives() -> list[dict[str, Any]]:
    if os.name != "nt":
        root = Path("/")
        return [{"path": str(root), "label": "/", "type": "fixed", "ready": True}]

    kernel32 = ctypes.windll.kernel32
    drive_mask = kernel32.GetLogicalDrives()
    drives: list[dict[str, Any]] = []
    for index in range(26):
        if not drive_mask & (1 << index):
            continue
        root = f"{chr(ord('A') + index)}:\\"
        drive_type_value = kernel32.GetDriveTypeW(ctypes.c_wchar_p(root))
        drive_type = DRIVE_TYPES.get(drive_type_value, "unknown")
        if drive_type in {"invalid", "cdrom"}:
            continue

        volume_name = ctypes.create_unicode_buffer(261)
        filesystem_name = ctypes.create_unicode_buffer(261)
        serial_number = ctypes.c_uint32()
        max_component_length = ctypes.c_uint32()
        filesystem_flags = ctypes.c_uint32()
        ready = bool(
            kernel32.GetVolumeInformationW(
                ctypes.c_wchar_p(root),
                volume_name,
                len(volume_name),
                ctypes.byref(serial_number),
                ctypes.byref(max_component_length),
                ctypes.byref(filesystem_flags),
                filesystem_name,
                len(filesystem_name),
            )
        )
        drives.append(
            {
                "path": root,
                "label": volume_name.value if ready else "",
                "type": drive_type,
                "ready": ready,
                "filesystem": filesystem_name.value if ready else "",
                "serial_number": f"{serial_number.value:08X}" if ready else None,
                "has_log_directory": ready and (Path(root) / "LOG").is_dir(),
            }
        )
    return drives


def browse_directory(path_value: str) -> dict[str, Any]:
    path = Path(path_value).expanduser().resolve()
    if not path.exists():
        raise FileNotFoundError(f"路径不存在：{path}")
    if path.is_file():
        path = path.parent
    if not path.is_dir():
        raise ValueError(f"不是目录：{path}")

    entries: list[dict[str, Any]] = []
    try:
        children = list(path.iterdir())
    except PermissionError as exc:
        raise PermissionError(f"没有权限读取目录：{path}") from exc

    for child in children:
        try:
            is_dir = child.is_dir()
            is_file = child.is_file()
            size = child.stat().st_size if is_file else None
        except OSError:
            continue
        is_log_file = is_file and LOG_FILE_PATTERN.fullmatch(child.name) is not None
        if not is_dir and not is_log_file:
            continue
        entries.append(
            {
                "name": child.name,
                "path": str(child),
                "is_dir": is_dir,
                "is_log_file": is_log_file,
                "size_bytes": size,
            }
        )

    entries.sort(
        key=lambda item: (
            0 if item["is_dir"] else 1,
            str(item["name"]).upper(),
        )
    )
    parent = path.parent if path.parent != path else None
    log_dir = path / "LOG"
    return {
        "path": str(path),
        "parent": str(parent) if parent else None,
        "entries": entries[:1000],
        "truncated": len(entries) > 1000,
        "has_log_directory": log_dir.is_dir(),
        "suggested_source": str(log_dir if log_dir.is_dir() else path),
    }
