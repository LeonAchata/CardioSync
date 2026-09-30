"""Lambda: triggered by S3 ObjectCreated on raw/<device>/<session>.bin.

Writes the analysis to OUTPUT_BUCKET under processed/<device>/<session>/.
"""

import logging
import os
from urllib.parse import unquote_plus

import boto3
from cardiosync import analyze

logger = logging.getLogger()
logger.setLevel(logging.INFO)

OUTPUT_PREFIX = os.environ.get("OUTPUT_PREFIX", "processed/")

s3 = boto3.client("s3")


def output_prefix_for(key: str) -> str:
    """raw/dev-1/session_1.bin -> processed/dev-1/session_1/"""
    relative = key.split("/", 1)[1] if key.startswith("raw/") else key
    stem = relative[:-4] if relative.endswith(".bin") else relative
    return f"{OUTPUT_PREFIX}{stem}/"


def process_object(bucket: str, key: str) -> dict:
    output_bucket = os.environ["OUTPUT_BUCKET"]
    logger.info("Processing s3://%s/%s", bucket, key)
    data = s3.get_object(Bucket=bucket, Key=key)["Body"].read()

    metadata, files = analyze(data, source=f"s3://{bucket}/{key}")

    prefix = output_prefix_for(key)
    for name, (content, content_type) in files.items():
        s3.put_object(Bucket=output_bucket, Key=prefix + name, Body=content, ContentType=content_type)

    avg_bpm = metadata["heart_rate"]["average_bpm"]
    logger.info(
        "Done: %.1f s, %.1f BPM -> s3://%s/%s",
        metadata["duration_seconds"],
        avg_bpm,
        output_bucket,
        prefix,
    )
    return {"source": key, "output_prefix": prefix, "average_bpm": avg_bpm}


def lambda_handler(event, context):
    # Exceptions propagate on purpose: S3 async invocations are retried and
    # then sent to the failure destination instead of being silently lost.
    results = [
        process_object(record["s3"]["bucket"]["name"], unquote_plus(record["s3"]["object"]["key"]))
        for record in event["Records"]
    ]
    return {"processed": results}
