import json
from urllib.parse import parse_qs, urlparse

import pytest
from conftest import load_lambda


@pytest.fixture
def app(monkeypatch):
    monkeypatch.setenv("RAW_BUCKET", "cardiosync-raw-test")
    monkeypatch.setenv("RESPONSE_TOPIC_PREFIX", "cardiosync/upload-url")
    monkeypatch.setenv("IOT_DATA_ENDPOINT", "example-ats.iot.us-east-1.amazonaws.com")
    monkeypatch.setenv("MAX_FILE_BYTES", "1000000")

    module = load_lambda("generate_upload_url")
    published = []

    class FakeIot:
        def publish(self, topic, qos, payload):
            published.append((topic, json.loads(payload)))

    monkeypatch.setattr(module, "iot_client", lambda: FakeIot())
    module.published = published
    return module


def test_issues_presigned_url_to_authenticated_device(app):
    event = {
        "device_id": "spoofed",
        "client_id": "esp32-001",
        "session_id": "session_1727712000",
        "file_size": 22528,
    }

    result = app.lambda_handler(event, None)

    assert result == {"status": "ok", "key": "raw/esp32-001/session_1727712000.bin"}
    topic, payload = app.published[0]
    assert topic == "cardiosync/upload-url/esp32-001"
    assert payload["session_id"] == "session_1727712000"

    url = urlparse(payload["upload_url"])
    assert url.hostname.startswith("cardiosync-raw-test.s3.")
    assert url.path == "/raw/esp32-001/session_1727712000.bin"
    query = parse_qs(url.query)
    assert query["X-Amz-Expires"] == ["300"]
    assert "content-type" in query["X-Amz-SignedHeaders"][0]


@pytest.mark.parametrize("session_id", [None, "", "../../etc", "a/b", "x" * 65])
def test_rejects_invalid_session_ids(app, session_id):
    result = app.lambda_handler({"client_id": "esp32-001", "session_id": session_id}, None)

    assert result["status"] == "rejected"
    assert app.published == [
        ("cardiosync/upload-url/esp32-001", {"session_id": session_id, "error": "invalid session_id"})
    ]


def test_rejects_invalid_device_without_publishing(app):
    result = app.lambda_handler({"client_id": "bad/device", "session_id": "session_1"}, None)

    assert result["status"] == "rejected"
    assert app.published == []


def test_rejects_oversized_files(app):
    event = {"client_id": "esp32-001", "session_id": "session_1", "file_size": 5_000_000}

    assert app.lambda_handler(event, None)["reason"] == "file too large"
    assert app.published[0][1]["error"] == "file too large"
