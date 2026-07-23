from fastapi.testclient import TestClient

from telemetry_viewer.app import app


def test_health_and_snapshot_endpoints() -> None:
    with TestClient(app) as client:
        health = client.get("/api/health")
        snapshot = client.get("/api/snapshot")

    assert health.status_code == 200
    assert health.json() == {"status": "ok"}
    assert snapshot.status_code == 200
    assert snapshot.json()["status"]["mode"] == "disconnected"


def test_demo_endpoint_starts_and_disconnects() -> None:
    with TestClient(app) as client:
        started = client.post(
            "/api/demo",
            json={"period_ms": 100, "inject_noise": True},
        )
        stopped = client.post("/api/disconnect", json={})

    assert started.status_code == 200
    assert started.json()["status"]["mode"] == "demo"
    assert stopped.status_code == 200
    assert stopped.json()["status"]["mode"] == "disconnected"

