// ======================================================
// ESP32-CAM CLIENT
// FINAL STABLE VERSION
//
// FLOW:
// Capture Image
// -> Upload To FastAPI
// -> Receive JSON
// -> Validate JSON
// -> Forward SAME JSON To Gateway ESP32
// -> Repeat
//
// NO LCD CODE HERE
// ======================================================

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "app_httpd.h"

#include "arduino_secrets.h"

// ======================================================
// CAMERA MODEL
// ======================================================

#define CAMERA_MODEL_AI_THINKER

#include "camera_pins.h"

// ======================================================
// WIFI
// ======================================================

const char* ssid = WIFI_USERNAME;
const char* password = WIFI_PASSWORD;

// ======================================================
// URLS FROM SECRETS
// ======================================================

const char* FASTAPI_URL =
FASTAPI_SERVER_URL;

String GATEWAY_URL =
  "http://" +
  String(ESP32_RECEIVER_IP) +
  ":" +
  String(ESP32_RECEIVER_PORT) +
  "/update_prediction";

// ======================================================
// SETTINGS
// ======================================================

const unsigned long CAPTURE_INTERVAL = 10000;

unsigned long lastCapture = 0;

// ======================================================
// HELPERS
// ======================================================

void logInfo(String tag, String msg) {

  Serial.print("[");
  Serial.print(tag);
  Serial.print("] ");

  Serial.println(msg);
}

void logError(String tag, String msg) {

  Serial.print("[ERROR][");
  Serial.print(tag);
  Serial.print("] ");

  Serial.println(msg);
}

// ======================================================
// WIFI CONNECT
// ======================================================

void connectWiFi() {

  logInfo("WIFI", "Connecting...");

  WiFi.begin(ssid, password);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");

    if (millis() - start > 20000) {

      Serial.println();

      logError("WIFI", "Connection timeout");

      delay(3000);

      ESP.restart();
    }
  }

  Serial.println();

  logInfo("WIFI", "CONNECTED");

  logInfo(
    "WIFI",
    "IP: " + WiFi.localIP().toString()
  );
}

// ======================================================
// CAMERA INIT
// ======================================================

bool initCamera() {

  logInfo("CAMERA", "Initializing...");

  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;

  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;

  config.pixel_format = PIXFORMAT_JPEG;

  // ==================================================
  // STABLE CAMERA SETTINGS
  // ==================================================

  config.frame_size = FRAMESIZE_QVGA;

  config.jpeg_quality = 12;

  config.fb_count = 1;

  config.fb_location = CAMERA_FB_IN_PSRAM;

  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {

    logError(
      "CAMERA",
      "Init failed: 0x" + String(err, HEX)
    );

    return false;
  }

  sensor_t * s = esp_camera_sensor_get();

  if (s == NULL) {

    logError(
      "CAMERA",
      "Sensor NULL"
    );

    return false;
  }

  s->set_brightness(s, 0);
  s->set_contrast(s, 0);
  s->set_saturation(s, 0);

  logInfo(
    "CAMERA",
    "Initialization SUCCESS"
  );

  return true;
}

// ======================================================
// CAPTURE IMAGE
// ======================================================

camera_fb_t* captureImage() {

  logInfo(
    "CAMERA",
    "Capturing image..."
  );

  camera_fb_t * fb =
    esp_camera_fb_get();

  if (!fb) {

    logError(
      "CAMERA",
      "Capture failed"
    );

    return NULL;
  }

  logInfo(
    "CAMERA",
    "Capture SUCCESS"
  );

  logInfo(
    "CAMERA",
    "Image size: " +
    String(fb->len) +
    " bytes"
  );

  return fb;
}

// ======================================================
// UPLOAD IMAGE TO FASTAPI
// ======================================================

String uploadImage(camera_fb_t * fb) {

  logInfo(
    "HTTP",
    "Uploading image to FastAPI"
  );

  HTTPClient http;

  http.setTimeout(RESPONSE_TIMEOUT);

  http.begin(FASTAPI_URL);

  String boundary =
    "----ESP32CAMBoundary";

  http.addHeader(
    "Content-Type",
    "multipart/form-data; boundary=" +
    boundary
  );

  // ==================================================
  // MULTIPART BODY
  // ==================================================

  String head =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; "
    "name=\"file\"; filename=\"capture.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";

  String tail =
    "\r\n--" + boundary + "--\r\n";

  uint32_t totalLen =
    head.length() +
    fb->len +
    tail.length();

  logInfo(
    "HTTP",
    "Multipart size: " +
    String(totalLen)
  );

  uint8_t * body =
    (uint8_t*) malloc(totalLen);

  if (!body) {

    logError(
      "MEMORY",
      "Multipart allocation failed"
    );

    http.end();

    return "";
  }

  memcpy(
    body,
    head.c_str(),
    head.length()
  );

  memcpy(
    body + head.length(),
    fb->buf,
    fb->len
  );

  memcpy(
    body + head.length() + fb->len,
    tail.c_str(),
    tail.length()
  );

  logInfo(
    "HTTP",
    "POST -> FastAPI"
  );

  int httpCode =
    http.POST(body, totalLen);

  // ==================================================
  // FREE MEMORY IMMEDIATELY
  // ==================================================

  free(body);

  body = NULL;

  // ==================================================
  // HTTP FAILURE
  // ==================================================

  if (httpCode <= 0) {

    logError(
      "HTTP",
      "POST failed: " +
      http.errorToString(httpCode)
    );

    http.end();

    return "";
  }

  logInfo(
    "HTTP",
    "FastAPI Response Code: " +
    String(httpCode)
  );

  String payload =
    http.getString();

  logInfo(
    "HTTP",
    "FastAPI Response Received"
  );

  Serial.println();
  Serial.println("========== FASTAPI RESPONSE ==========");
  Serial.println(payload);
  Serial.println("======================================");
  Serial.println();

  http.end();

  return payload;
}

// ======================================================
// VALIDATE JSON
// ======================================================

bool validateJSON(String payload) {

  if (payload.length() == 0) {

    logError(
      "JSON",
      "Empty payload"
    );

    return false;
  }

  DynamicJsonDocument doc(2048);

  DeserializationError error =
    deserializeJson(doc, payload);

  if (error) {

    logError(
      "JSON",
      error.c_str()
    );

    return false;
  }

  logInfo(
    "JSON",
    "Parse SUCCESS"
  );

  String status =
    doc["status"] | "unknown";

  logInfo(
    "JSON",
    "Status: " + status
  );

  if (status == "accepted") {

    String crop =
      doc["crop"] | "Unknown";

    String disease =
      doc["disease"] | "Unknown";

    float confidence =
      doc["confidence"] | 0.0;

    Serial.println("--------------------------------");
    Serial.println("PREDICTION ACCEPTED");
    Serial.println("--------------------------------");

    Serial.println("Crop: " + crop);

    Serial.println("Disease: " + disease);

    Serial.print("Confidence: ");

    Serial.println(confidence);

    Serial.println("--------------------------------");
  }

  else if (status == "rejected") {

    String reason =
      doc["reason"] | "Unknown";

    Serial.println("--------------------------------");
    Serial.println("PREDICTION REJECTED");
    Serial.println("--------------------------------");

    Serial.println("Reason: " + reason);

    Serial.println("--------------------------------");
  }

  else {

    logError(
      "JSON",
      "Unknown status"
    );

    return false;
  }

  return true;
}

// ======================================================
// FORWARD JSON TO GATEWAY
// ======================================================

bool forwardToGateway(String payload) {

  logInfo(
    "GATEWAY",
    "Forwarding JSON..."
  );

  logInfo(
    "GATEWAY",
    "URL: " + GATEWAY_URL
  );

  HTTPClient http;

  http.setTimeout(RESPONSE_TIMEOUT);

  http.begin(GATEWAY_URL.c_str());

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  int httpCode =
    http.POST(payload);

  if (httpCode <= 0) {

    logError(
      "GATEWAY",
      "POST failed: " +
      http.errorToString(httpCode)
    );

    http.end();

    return false;
  }

  logInfo(
    "GATEWAY",
    "Response Code: " +
    String(httpCode)
  );

  String response =
    http.getString();

  logInfo(
    "GATEWAY",
    "Gateway Response: " + response
  );

  http.end();

  return true;
}

// ======================================================
// FULL PIPELINE
// ======================================================

void runPipeline() {

  Serial.println();
  Serial.println("======================================");
  Serial.println("STARTING NEW PIPELINE CYCLE");
  Serial.println("======================================");

  // ==================================================
  // WIFI CHECK
  // ==================================================

  if (WiFi.status() != WL_CONNECTED) {

    logError(
      "WIFI",
      "Disconnected"
    );

    connectWiFi();
  }

  // ==================================================
  // CAPTURE IMAGE
  // ==================================================

  camera_fb_t * fb =
    captureImage();

  if (!fb) {

    logError(
      "PIPELINE",
      "Capture stage failed"
    );

    return;
  }

  // ==================================================
  // UPLOAD IMAGE
  // ==================================================

  String payload =
    uploadImage(fb);

  // ==================================================
  // RELEASE CAMERA BUFFER ASAP
  // ==================================================

  esp_camera_fb_return(fb);

  logInfo(
    "CAMERA",
    "Frame buffer released"
  );

  // ==================================================
  // EMPTY RESPONSE
  // ==================================================

  if (payload.length() == 0) {

    logError(
      "PIPELINE",
      "FastAPI returned empty payload"
    );

    return;
  }

  // ==================================================
  // JSON VALIDATION
  // ==================================================

  bool valid =
    validateJSON(payload);

  if (!valid) {

    logError(
      "PIPELINE",
      "JSON validation failed"
    );

    return;
  }

  // ==================================================
  // FORWARD TO GATEWAY
  // ==================================================

  bool forwarded =
    forwardToGateway(payload);

  if (!forwarded) {

    logError(
      "PIPELINE",
      "Gateway forwarding failed"
    );

    return;
  }

  // ==================================================
  // SUCCESS
  // ==================================================

  logInfo(
    "PIPELINE",
    "FULL SUCCESS"
  );

  Serial.println("======================================");
  Serial.println("PIPELINE COMPLETE");
  Serial.println("======================================");
  Serial.println();
}

// ======================================================
// SETUP
// ======================================================

void setup() {
  
  Serial.begin(115200);
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  delay(3000);

  Serial.println();
  Serial.println("======================================");
  Serial.println("ESP32-CAM CLIENT");
  Serial.println("SMART FARMING SYSTEM");
  Serial.println("======================================");

  // ==================================================
  // WIFI
  // ==================================================

  connectWiFi();

  // ==================================================
  // CAMERA
  // ==================================================

  bool camOK =
    initCamera();

  if (!camOK) {

    logError(
      "SYSTEM",
      "Camera init failed"
    );

    delay(5000);

    ESP.restart();
  }
  startCameraServer();
  logInfo(
    "SYSTEM",
    "BOOT COMPLETE"
  );
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  // ==================================================
  // PERIODIC CAPTURE
  // ==================================================

  if (
    millis() - lastCapture
    >= CAPTURE_INTERVAL
  ) {

    lastCapture = millis();

    runPipeline();
  }

  // ==================================================
  // MEMORY LOGGING
  // ==================================================

  static unsigned long lastHeapLog = 0;

  if (
    millis() - lastHeapLog
    > 30000
  ) {

    lastHeapLog = millis();

    logInfo(
      "SYSTEM",
      "Free Heap: " +
      String(ESP.getFreeHeap())
    );

    logInfo(
      "SYSTEM",
      "WiFi RSSI: " +
      String(WiFi.RSSI())
    );
  }

  delay(10);
}