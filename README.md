# Smart Visitor Counter

![Architecture](docs/architecture.png)

## Live demo
- Dashboard: http://visitor-counter-dashboard-stephen123.s3-website-us-east-1.amazonaws.com
- API (read): https://dl4dqwtta9.execute-api.us-east-1.amazonaws.com/count
- API (increment): `POST https://dl4dqwtta9.execute-api.us-east-1.amazonaws.com/increment`

Built in an AWS Academy Learner Lab (us-east-1) using S3, API Gateway, Lambda,
DynamoDB and IoT Core. The dashboard has an "I'm here" web button
(`lambda/increment_web.py`); the ESP32 + IoT Core path is designed and included
but was not flashed or tested.

A physical push button on an ESP32 increments a live counter shown on a
public dashboard. No polling of the device itself — the ESP32 only ever
*publishes*; everything downstream is serverless.

```
[Push Button] --GPIO--> [ESP32] --MQTT/TLS--> [AWS IoT Core]
                                                     |
                                                     | IoT Rule
                                                     v
                                          [Lambda: increment_counter]
                                                     |
                                                     v
                                            [DynamoDB: VisitorCounter]
                                                     ^
                                                     | GetItem
                                          [Lambda: get_count]
                                                     ^
                                                     | HTTP GET
                                          [API Gateway (HTTP API)]
                                                     ^
                                                     | fetch() every 2s
                                          [S3 static site: dashboard]
```

Files in this project:

| Path | Purpose |
|---|---|
| `firmware/visitor_counter.ino` | ESP32 sketch — debounces the button, publishes an MQTT message per press |
| `lambda/increment_counter.py` | Triggered by an IoT Core rule, atomically increments the DynamoDB counter |
| `lambda/increment_web.py` | Triggered by the dashboard button via API Gateway (POST /increment) |
| `lambda/get_count.py` | Triggered by API Gateway, returns the current count as JSON |
| `infra/dynamodb-table.json` | Table definition (also given as an `aws dynamodb create-table` command below) |
| `infra/iot-policy.json` | IoT policy attached to the device certificate (publish-only, least privilege) |
| `infra/lambda-increment-role-policy.json` | Permissions for the increment Lambda |
| `infra/lambda-getcount-role-policy.json` | Permissions for the get-count Lambda |
| `dashboard/index.html` | The S3-hosted static dashboard (self-contained, no build step) |

---

## 1. DynamoDB — the counter's source of truth

```bash
aws dynamodb create-table \
  --table-name VisitorCounter \
  --attribute-definitions AttributeName=counterID,AttributeType=S \
  --key-schema AttributeName=counterID,KeyType=HASH \
  --billing-mode PAY_PER_REQUEST

# Seed the row the counter will increment
aws dynamodb put-item \
  --table-name VisitorCounter \
  --item '{"counterID": {"S": "total"}, "count": {"N": "0"}}'
```

One item, `counterID = "total"`, holds a single `count` attribute. Using
`ADD count :incr` (see the Lambda) makes each increment atomic — concurrent
button presses can never clobber each other, even with many devices.

## 2. IoT Core — register the ESP32 as a "Thing"

```bash
# Create the thing
aws iot create-thing --thing-name esp32-visitor-counter-01

# Create a certificate + keys (SAVE the output — the private key is only shown once)
aws iot create-keys-and-certificate --set-as-active \
  --certificate-pem-outfile device-cert.pem.crt \
  --public-key-outfile device-public.pem.key \
  --private-key-outfile device-private.pem.key

# Create and attach the least-privilege policy (see infra/iot-policy.json)
aws iot create-policy --policy-name VisitorCounterPublishOnly \
  --policy-document file://infra/iot-policy.json
aws iot attach-policy --policy-name VisitorCounterPublishOnly \
  --target <CERTIFICATE_ARN from previous step>
aws iot attach-thing-principal --thing-name esp32-visitor-counter-01 \
  --principal <CERTIFICATE_ARN>

# Get your account's IoT endpoint (goes into the firmware)
aws iot describe-endpoint --endpoint-type iot:Data-ATS
```

Also download the Amazon Root CA 1 cert (`AmazonRootCA1.pem`) from
`https://www.amazontrust.com/repository/AmazonRootCA1.pem` — the firmware
needs it to verify AWS's TLS certificate.

`infra/iot-policy.json` restricts the device to publishing on exactly
`visitorcounter/esp32-visitor-counter-01/press` — a compromised device
can't subscribe, publish elsewhere, or read other topics.

## 3. Lambda — increment on press

```bash
zip increment_counter.zip lambda/increment_counter.py
aws lambda create-function \
  --function-name visitorCounterIncrement \
  --runtime python3.13 \
  --handler increment_counter.handler \
  --zip-file fileb://increment_counter.zip \
  --role <ARN of a role using infra/lambda-increment-role-policy.json>
```

Wire it to IoT Core with a topic rule:

```bash
aws iot create-topic-rule --rule-name VisitorCounterIncrementRule \
  --topic-rule-payload '{
    "sql": "SELECT * FROM \"visitorcounter/+/press\"",
    "actions": [{
      "lambda": { "functionArn": "<increment Lambda ARN>" }
    }]
  }'

aws lambda add-permission \
  --function-name visitorCounterIncrement \
  --statement-id iotRuleInvoke \
  --action lambda:InvokeFunction \
  --principal iot.amazonaws.com \
  --source-arn <ARN of the topic rule>
```

## 4. Lambda + API Gateway — read the count

```bash
zip get_count.zip lambda/get_count.py
aws lambda create-function \
  --function-name visitorCounterGet \
  --runtime python3.13 \
  --handler get_count.handler \
  --zip-file fileb://get_count.zip \
  --role <ARN of a role using infra/lambda-getcount-role-policy.json>

# HTTP API (cheaper & simpler than REST API for a single GET route)
aws apigatewayv2 create-api \
  --name VisitorCounterApi \
  --protocol-type HTTP \
  --target <get_count Lambda ARN> \
  --route-key "GET /count" \
  --cors-configuration AllowOrigins="*",AllowMethods=GET
```

`create-api --target` auto-creates the integration, route, and required
`lambda:InvokeFunction` permission in one step. Grab the resulting
`ApiEndpoint` — that's the `API_URL` the dashboard polls.

Tighten `AllowOrigins` to your actual S3/CloudFront origin once the
dashboard is deployed, instead of leaving it as `*`.

## 5. S3 — host the dashboard

```bash
aws s3 mb s3://your-visitor-counter-dashboard
aws s3 website s3://your-visitor-counter-dashboard --index-document index.html

# Edit dashboard/index.html first and set API_URL to your API Gateway endpoint
aws s3 cp dashboard/index.html s3://your-visitor-counter-dashboard/index.html \
  --content-type text/html
```

Attach a bucket policy allowing `s3:GetObject` to `*` (or, better, put
CloudFront in front of the bucket with Origin Access Control and skip
public bucket access entirely).

## 6. Firmware

Open `firmware/visitor_counter.ino` in the Arduino IDE (or PlatformIO)
with the ESP32 board package installed. Fill in:

- `WIFI_SSID` / `WIFI_PASSWORD`
- `AWS_IOT_ENDPOINT` (from `aws iot describe-endpoint`)
- The three certs (`AmazonRootCA1.pem`, `device-cert.pem.crt`, `device-private.pem.key`) as PROGMEM strings
- `BUTTON_PIN` — defaults to GPIO 4, wired to a push button pulled to
  GND (uses `INPUT_PULLUP`, so no external resistor needed)

Flash it, open the button, and the dashboard should tick up within
~2 seconds of each press.

## Notes on "live"

The dashboard polls every 2 seconds rather than holding an open
connection — far simpler to build and secure than authenticating a
browser directly against IoT Core (which needs Cognito + SigV4 signing
for MQTT-over-WebSocket). If you want true push updates later, swap
the polling `fetch()` for AWS IoT's browser SDK subscribed to the same
topic, using a Cognito Identity Pool for temporary credentials.

## Cost

At visitor-counter scale (a few hundred presses a day) this runs
comfortably in AWS's always-free tier: IoT Core, Lambda, DynamoDB
on-demand, and API Gateway HTTP API all have generous free allowances;
S3 static hosting is pennies a month.
