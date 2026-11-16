"""
visitorCounterGet

Triggered by API Gateway (HTTP API, Lambda proxy integration) on
GET /count. Reads the current counter value from DynamoDB and
returns it as JSON for the S3-hosted dashboard to poll.
"""

import os
import json
import boto3

TABLE_NAME = os.environ.get("TABLE_NAME", "VisitorCounter")
COUNTER_ID = os.environ.get("COUNTER_ID", "total")

# Restrict this to your dashboard's real origin once deployed,
# e.g. "https://your-visitor-counter-dashboard.s3-website-us-east-1.amazonaws.com"
ALLOWED_ORIGIN = os.environ.get("ALLOWED_ORIGIN", "*")

dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(TABLE_NAME)

CORS_HEADERS = {
    "Access-Control-Allow-Origin": ALLOWED_ORIGIN,
    "Access-Control-Allow-Methods": "GET,OPTIONS",
    "Content-Type": "application/json",
}


def handler(event, context):
    try:
        response = table.get_item(Key={"counterID": COUNTER_ID})
        item = response.get("Item")
        count = int(item["count"]) if item else 0
        last_press_at = item.get("lastPressAt") if item else None

        return {
            "statusCode": 200,
            "headers": CORS_HEADERS,
            "body": json.dumps({"count": count, "lastPressAt": last_press_at}),
        }
    except Exception as exc:  # noqa: BLE001 — surface any failure as a clean 500
        print(f"Error reading counter: {exc}")
        return {
            "statusCode": 500,
            "headers": CORS_HEADERS,
            "body": json.dumps({"error": "Could not read counter"}),
        }
