"""Lambda: invoked by the AWS IoT rule on <prefix>/upload-request.

Generates an S3 presigned PUT URL for the session file and publishes it back to
the device on <prefix>/upload-url/<device_id>.

Expected event (device payload + fields injected by the IoT rule SQL):
    {"device_id": "...", "session_id": "session_1727712000", "file_size": 22528,
     "timestamp": 1727712000, "client_id": "<MQTT client id>"}
"""

import json
import logging
import os
import re
from functools import cache

import boto3
from botocore.config import Config

logger = logging.getLogger()
logger.setLevel(logging.INFO)

SAFE_ID = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
CONTENT_TYPE = "application/octet-stream"


@cache
def s3_client():
    # SigV4 + virtual-hosted style gives a regional URL the ESP32 can PUT to directly
    return boto3.client("s3", config=Config(signature_version="s3v4", s3={"addressing_style": "virtual"}))


@cache
def iot_client():
    return boto3.client("iot-data", endpoint_url=f"https://{os.environ['IOT_DATA_ENDPOINT']}")


def publish(device_id: str, payload: dict) -> None:
    topic = f"{os.environ['RESPONSE_TOPIC_PREFIX']}/{device_id}"
    iot_client().publish(topic=topic, qos=1, payload=json.dumps(payload))


def lambda_handler(event, context):
    # client_id comes from the authenticated MQTT connection (IoT rule: clientid()),
    # so a device cannot request URLs on behalf of another one.
    device_id = event.get("client_id") or event.get("device_id")
    session_id = event.get("session_id")

    if not device_id or not SAFE_ID.match(device_id):
        logger.error("Rejected request with invalid device id: %r", device_id)
        return {"status": "rejected", "reason": "invalid device_id"}

    if not session_id or not SAFE_ID.match(session_id):
        logger.error("Rejected request from %s with invalid session id: %r", device_id, session_id)
        publish(device_id, {"session_id": session_id, "error": "invalid session_id"})
        return {"status": "rejected", "reason": "invalid session_id"}

    max_bytes = int(os.environ.get("MAX_FILE_BYTES", "0"))
    file_size = int(event.get("file_size") or 0)
    if max_bytes and file_size > max_bytes:
        logger.error("Rejected %s/%s: %d bytes > %d", device_id, session_id, file_size, max_bytes)
        publish(device_id, {"session_id": session_id, "error": "file too large"})
        return {"status": "rejected", "reason": "file too large"}

    key = f"raw/{device_id}/{session_id}.bin"
    expires = int(os.environ.get("URL_EXPIRES_SECONDS", "300"))
    url = s3_client().generate_presigned_url(
        "put_object",
        Params={"Bucket": os.environ["RAW_BUCKET"], "Key": key, "ContentType": CONTENT_TYPE},
        ExpiresIn=expires,
    )

    publish(device_id, {"session_id": session_id, "upload_url": url, "key": key, "expires_in": expires})
    logger.info("Issued upload URL for s3://%s/%s (%d bytes)", os.environ["RAW_BUCKET"], key, file_size)
    return {"status": "ok", "key": key}
