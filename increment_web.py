"""
visitorCounterIncrementWeb

Triggered by API Gateway (HTTP API, Lambda proxy integration) on
POST /increment. Does the exact same atomic increment as the
IoT-triggered Lambda, but is called directly by a button click in
the browser instead of by a physical button press over MQTT.
"""

import os
import json
import boto3
from datetime import datetime, timezone

TABLE_NAME = os.environ.get("TABLE_NAME", "VisitorCounter")
COUNTER_ID = os.environ.get("COUNTER_ID", "total")

# Restrict this to your dashboard's real origin once deployed.
ALLOWED_ORIGIN = os.environ.get("ALLOWED_ORIGIN", "*")

dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(TABLE_NAME)

CORS_HEADERS = {
    "Access-Control-Allow-Origin": ALLOWED_ORIGIN,
    "Access-Control-Allow-Methods": "POST,OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
    "Content-Type": "application/json",
}


def handler(event, context):
    # API Gateway sends an OPTIONS preflight request before the real POST;
    # respond to it with just the CORS headers and no body.
    method = event.get("requestContext", {}).get("http", {}).get("method", "")
    if method == "OPTIONS":
        return {"statusCode": 200, "headers": CORS_HEADERS, "body": ""}

    try:
        response = table.update_item(
            Key={"counterID": COUNTER_ID},
            UpdateExpression="ADD #c :incr SET lastDeviceId = :dev, lastPressAt = :ts",
            ExpressionAttributeNames={"#c": "count"},
            ExpressionAttributeValues={
                ":incr": 1,
                ":dev": "web-button",
                ":ts": datetime.now(timezone.utc).isoformat(),
            },
            ReturnValues="UPDATED_NEW",
        )
        new_count = int(response["Attributes"]["count"])

        return {
            "statusCode": 200,
            "headers": CORS_HEADERS,
            "body": json.dumps({"count": new_count}),
        }
    except Exception as exc:
        print(f"Error incrementing counter: {exc}")
        return {
            "statusCode": 500,
            "headers": CORS_HEADERS,
            "body": json.dumps({"error": "Could not increment counter"}),
        }
