#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>      // https://github.com/bblanchon/ArduinoJson
#include <time.h>
#include <SPI.h>
#include <MFRC522.h>
#include <SoftwareSerial.h>

//────────────────────────────
// Pins
//────────────────────────────
#define RST_PIN     D0
#define SS_PIN      D4
#define ARDUINO_RX  D1
#define ARDUINO_TX  D2

//────────────────────────────
// Wi‑Fi credentials
//────────────────────────────
const char* ssid     = "Raiders Pass";
const char* password = "8884pavlov";

//────────────────────────────
// Firebase Realtime DB info
//────────────────────────────
const char*  fbHost = "smart-dustbin-c601d-default-rtdb.firebaseio.com";
const String fbPath = "/trashLogs.json";

//────────────────────────────
// Fixed “full” distance threshold
//────────────────────────────
const int TRASH_FULL_DISTANCE = 10;

//────────────────────────────
// RFID reader & serial link
//────────────────────────────
SoftwareSerial arduinoSerial(ARDUINO_TX, ARDUINO_RX);
MFRC522 mfrc522(SS_PIN, RST_PIN);

//────────────────────────────
// Authorized UIDs
//────────────────────────────
const String authorizedTags[] = {
  "B3ED092D",
  "2583F600"
};

//────────────────────────────
// Helper: ISO8601 CDT timestamp
//────────────────────────────
String getFormattedTime() {
  time_t now = time(nullptr);
  struct tm *tminfo = localtime(&now);
  char buf[30];
  // %z will expand to ±HHMM offset (e.g. -0500 for CDT)
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S%z", tminfo);
  return String(buf);
}

//────────────────────────────
// Push one JSON record into RTDB
//────────────────────────────
void sendToFirebase(int distance, float temp, float hum, const char* status) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected, skipping Firebase log");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String("https://") + fbHost + fbPath;
  if (!http.begin(client, url)) {
    Serial.println("❌ HTTPClient.begin() failed");
    return;
  }
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["timestamp"]    = getFormattedTime();
  doc["distance_cm"]  = distance;
  doc["temperature"]  = temp;
  doc["humidity"]     = hum;
  doc["status"]       = status;

  String payload;
  serializeJson(doc, payload);
  Serial.println("➡️  Payload: " + payload);

  int httpCode = http.POST(payload);
  Serial.printf("Firebase POST code: %d (%s)\n",
                httpCode, http.errorToString(httpCode).c_str());
  http.end();
}

void setup() {
  Serial.begin(9600);
  arduinoSerial.begin(9600);

  // RFID init
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("🔐 RFID Reader Ready");

  // Connect Wi‑Fi
  WiFi.begin(ssid, password);
  Serial.print("📶 Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ Connected to WiFi");

  // Sync time & set CST/CDT timezone rules
  //   (handles both standard and daylight savings automatically)
  configTzTime(
    "CST6CDT,M3.2.0/02:00:00,M11.1.0/02:00:00",
    "pool.ntp.org", "time.nist.gov"
  );

  // Wait until the epoch is > Jan&nbsp;1&nbsp;2020
  while (time(nullptr) < 1577836800) {
    Serial.print("*");
    delay(200);
  }
  Serial.println("\n✅ Time synchronized");
}

void loop() {
  // — Handle messages from main Arduino —
  if (arduinoSerial.available()) {
    String msg = arduinoSerial.readStringUntil('\n');
    msg.trim();
    Serial.println("From Arduino: " + msg);

    if (msg.startsWith("TRASH_FULL") || msg.startsWith("LID_OPENED")) {
      // parse CSV: STATUS,TEMP,HUM
      int comma1 = msg.indexOf(',');
      int comma2 = msg.indexOf(',', comma1+1);
      String status = msg.substring(0, comma1);
      float temp    = msg.substring(comma1+1, comma2).toFloat();
      float hum     = msg.substring(comma2+1).toFloat();
      int dist      = (status == "TRASH_FULL") ? TRASH_FULL_DISTANCE : 0;

      Serial.println("Logging to Firebase…");
      sendToFirebase(dist, temp, hum, status.c_str());
    }
  }

  // — RFID scan → OPEN_LID back to Arduino —
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String uid;
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) uid += "0";
      uid += String(mfrc522.uid.uidByte[i], HEX);
    }
    uid.toUpperCase();
    Serial.println("Scanned UID: " + uid);

    for (auto &tag : authorizedTags) {
      if (uid == tag) {
        Serial.println("✅ Authorized! Sending OPEN_LID");
        arduinoSerial.println("OPEN_LID");
        delay(100);
        break;
      }
    }
    mfrc522.PICC_HaltA();
    delay(500);
  }
}