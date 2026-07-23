from __future__ import annotations

import csv
import io
import json

from fastapi.testclient import TestClient

from sd_log_reader.app import app

from .helpers import make_record, write_log


def test_load_page_and_exports(tmp_path) -> None:
    log_dir = tmp_path / "LOG"
    log_dir.mkdir()
    write_log(
        log_dir / "LOG00000.BIN",
        [make_record(sequence=1), make_record(sequence=2, tick_ms=11000)],
    )

    with TestClient(app) as client:
        response = client.post("/api/load", json={"path": str(tmp_path)})
        assert response.status_code == 200
        payload = response.json()
        assert payload["summary"]["total_records"] == 2
        assert payload["files"][0]["clean"] is True

        page = client.get("/api/records", params={"offset": 1, "limit": 10})
        assert page.status_code == 200
        assert page.json()["records"][0]["sequence"] == 2

        csv_response = client.get("/api/export.csv")
        assert csv_response.status_code == 200
        rows = list(csv.DictReader(io.StringIO(csv_response.text.lstrip("\ufeff"))))
        assert [row["sequence"] for row in rows] == ["1", "2"]

        json_response = client.get("/api/export.json")
        assert json_response.status_code == 200
        exported = json.loads(json_response.text)
        assert exported["summary"]["total_records"] == 2
        assert len(exported["records"]) == 2


def test_browse_lists_only_directories_and_log_files(tmp_path) -> None:
    (tmp_path / "LOG").mkdir()
    (tmp_path / "ignore.txt").write_text("ignored", encoding="utf-8")
    write_log(tmp_path / "LOG00001.BIN", [make_record()], file_index=1)

    with TestClient(app) as client:
        response = client.get("/api/browse", params={"path": str(tmp_path)})
        assert response.status_code == 200
        names = [entry["name"] for entry in response.json()["entries"]]
        assert names == ["LOG", "LOG00001.BIN"]
