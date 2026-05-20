// ======================================================
// ESP32 GATEWAY SERVER + I2C LCD
// FINAL CLEAN WORKING VERSION
// ======================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#include "arduino_secrets.h"

// ======================================================
// WIFI
// ======================================================

const char* ssid = WIFI_USERNAME;
const char* password = WIFI_PASSWORD;

// ======================================================
// SERVER
// ======================================================

WebServer server(80);

// ======================================================
// LCD
// ======================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

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
// LCD HELPERS
// ======================================================

void clearLine(int line) {

  lcd.setCursor(0, line);

  lcd.print("                ");

  lcd.setCursor(0, line);
}

void lcdMessage(
  String line1,
  String line2
) {

  lcd.clear();

  delay(5);

  clearLine(0);
  clearLine(1);

  lcd.setCursor(0, 0);
  lcd.print(line1);

  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// ======================================================
// ACCEPTED DISPLAY
// ======================================================

void displayAccepted(
  String crop,
  String disease,
  float confidence
) {

  lcd.clear();

  delay(5);

  clearLine(0);
  clearLine(1);

  // =====================================
  // LINE 1
  // =====================================

  lcd.setCursor(0, 0);

  lcd.print(
    String(confidence * 100.0, 0)
  );

  lcd.print("% | ");

  lcd.print(crop);

  // =====================================
  // LINE 2
  // =====================================

  lcd.setCursor(0, 1);

  lcd.print(disease);

  // =====================================
  // SERIAL LOG
  // =====================================

  Serial.println("================================");

  Serial.println("ACCEPTED");

  Serial.print("Crop: ");
  Serial.println(crop);

  Serial.print("Disease: ");
  Serial.println(disease);

  Serial.print("Confidence: ");
  Serial.println(confidence);

  Serial.println("================================");
}

// ======================================================
// REJECTED DISPLAY
// ======================================================

void displayRejected(
  String reason
) {

  lcd.clear();

  delay(5);

  clearLine(0);
  clearLine(1);

  lcd.setCursor(0, 0);

  lcd.print("REJECTED");

  lcd.setCursor(0, 1);

  lcd.print(reason);

  logInfo(
    "LCD",
    "Rejected displayed"
  );
}

// ======================================================
// WIFI CONNECT
// ======================================================

void connectWiFi() {

  logInfo(
    "WIFI",
    "Connecting..."
  );

  lcdMessage(
    "Connecting...",
    "WiFi"
  );

  WiFi.begin(ssid, password);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);

    Serial.print(".");

    if (millis() - start > 20000) {

      Serial.println();

      logError(
        "WIFI",
        "Timeout"
      );

      lcdMessage(
        "WiFi Failed",
        "Restarting"
      );

      delay(3000);

      ESP.restart();
    }
  }

  Serial.println();

  logInfo(
    "WIFI",
    "Connected"
  );

  logInfo(
    "WIFI",
    WiFi.localIP().toString()
  );

  lcdMessage(
    "WiFi Connected",
    WiFi.localIP().toString()
  );

  delay(2000);

  lcd.clear();
}

// ======================================================
// HEALTH CHECK
// ======================================================

void healthCheck() {

  logInfo(
    "HTTP",
    "Health check"
  );

  server.send(
    200,
    "text/plain",
    "Gateway Alive"
  );
}

// ======================================================
// RECEIVE PREDICTION
// ======================================================

void receivePrediction() {

  Serial.println();
  Serial.println("====================================");
  Serial.println("[HTTP] POST /update_prediction");
  Serial.println("====================================");

  // =====================================
  // BODY CHECK
  // =====================================

  if (!server.hasArg("plain")) {

    logError(
      "HTTP",
      "No body"
    );

    lcdMessage(
      "HTTP ERROR",
      "NO BODY"
    );

    server.send(
      400,
      "text/plain",
      "NO BODY"
    );

    return;
  }

  String body = server.arg("plain");

  if (body.length() == 0) {

    logError(
      "HTTP",
      "Empty body"
    );

    lcdMessage(
      "HTTP ERROR",
      "EMPTY BODY"
    );

    server.send(
      400,
      "text/plain",
      "EMPTY BODY"
    );

    return;
  }

  // =====================================
  // RAW BODY
  // =====================================

  logInfo(
    "HTTP",
    "RAW BODY:"
  );

  Serial.println(body);

  // =====================================
  // JSON PARSE
  // =====================================

  DynamicJsonDocument doc(2048);

  DeserializationError error =
      deserializeJson(doc, body);

  if (error) {

    logError(
      "JSON",
      error.c_str()
    );

    lcdMessage(
      "JSON ERROR",
      "PARSE FAIL"
    );

    server.send(
      400,
      "text/plain",
      "JSON ERROR"
    );

    return;
  }

  logInfo(
    "JSON",
    "Parse success"
  );

  // =====================================
  // FIELD EXTRACTION
  // =====================================

  String status =
      doc["status"] | "unknown";

  String crop =
      doc["crop"] | "Unknown";

  String disease =
      doc["disease"] | "Unknown";

  String reason =
      doc["reason"] | "No reason";

  float confidence =
      doc["confidence"] | 0.0;

  // =====================================
  // LOG EVERYTHING
  // =====================================

  Serial.println("------------------------------------");

  Serial.println("[PREDICTION DATA]");

  Serial.println("Status: " + status);

  Serial.println("Crop: " + crop);

  Serial.println("Disease: " + disease);

  Serial.print("Confidence: ");
  Serial.println(confidence);

  Serial.println("Reason: " + reason);

  Serial.println("------------------------------------");

  // =====================================
  // DISPLAY
  // =====================================

  if (status == "accepted") {

    displayAccepted(
      crop,
      disease,
      confidence
    );
  }

  else if (status == "rejected") {

    displayRejected(reason);
  }

  else {

    logError(
      "JSON",
      "Unknown status"
    );

    lcdMessage(
      "BAD STATUS",
      status
    );
  }

  // =====================================
  // RESPONSE
  // =====================================

  server.send(
    200,
    "text/plain",
    "OK"
  );

  logInfo(
    "HTTP",
    "200 OK SENT"
  );

  Serial.println("====================================");
}

// ======================================================
// SETUP
// ======================================================

void setup() {

  Serial.begin(115200);

  delay(2000);

  Serial.println();
  Serial.println("====================================");
  Serial.println("SMART FARMING GATEWAY");
  Serial.println("====================================");

  // =====================================
  // I2C
  // =====================================

  Wire.begin();

  // =====================================
  // LCD
  // =====================================

  lcd.init();

  lcd.backlight();

  lcdMessage(
    "Smart Farming",
    "Gateway Boot"
  );

  delay(2000);

  // =====================================
  // WIFI
  // =====================================

  connectWiFi();

  // =====================================
  // ROUTES
  // =====================================

  server.on(
    "/health",
    HTTP_GET,
    healthCheck
  );

  server.on(
    "/update_prediction",
    HTTP_POST,
    receivePrediction
  );

  // =====================================
  // SERVER START
  // =====================================

  server.begin();

  logInfo(
    "HTTP",
    "Gateway started"
  );

  Serial.print("Gateway URL: http://");

  Serial.println(WiFi.localIP());

  lcdMessage(
    "Gateway Ready",
    WiFi.localIP().toString()
  );

  delay(2000);

  lcd.clear();
}

// ======================================================
// LOOP
// ======================================================

void loop() {

  server.handleClient();

  delay(2);
}