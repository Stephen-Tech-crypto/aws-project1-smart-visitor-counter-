"""
visitorCounterIncrement

Triggered directly by an AWS IoT Core topic rule (no API Gateway in
this path — IoT Core invokes Lambda natively). The rule's SQL passes
the parsed MQTT JSON payload straight through as the Lambda event.

Atomically increments a single counter item in DynamoDB using an
UpdateItem ADD expression, so concurrent presses from multiple
devices can never race and lose an increment.
"""

import os
import boto3

TABLE_NAME = os.environ.get("TABLE_NAME", "VisitorCounter")
COUNTER_ID = os.environ.get("COUNTER_ID", "total")

dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(TABLE_NAME)


def handler(event, context):
    device_id = event.get("device_id", "unknown")

    response = table.update_item(
        Key={"counterID": COUNTER_ID},
        UpdateExpression="ADD #c :incr SET lastDeviceId = :dev, lastPressAt = :ts",
        ExpressionAttributeNames={"#c": "count"},
        ExpressionAttributeValues={
            ":incr": 1,
            ":dev": device_id,
            ":ts": context.aws_request_id and _now_iso(),
        },
        ReturnValues="UPDATED_NEW",
    )

    new_count = response["Attributes"]["count"]
    print(f"Incremented counter '{COUNTER_ID}' to {new_count} (device: {device_id})")
    return {"count": int(new_count)}


def _now_iso():
    from datetime import datetime, timezone
    return datetime.now(timezone.utc).isoformat()
