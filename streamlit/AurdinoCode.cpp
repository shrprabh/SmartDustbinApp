#include <Servo.h>
#include <dht11.h>
#include <SoftwareSerial.h>

// === Pin Configuration ===
#define PIR_PIN 2
#define TRIG_PIN 3
#define ECHO_PIN 4
#define SERVO_PIN 5
#define DHT_PIN 6
#define BUZZER_PIN 7
#define ESP_RX 10
#define ESP_TX 11

SoftwareSerial espSerial(ESP_RX, ESP_TX);
Servo lidServo;
dht11 DHT11;

// === Thresholds and Timing ===
const int TRASH_FULL_DISTANCE = 10;
const float TEMP_ALARM_THRESHOLD = 20.0;
const unsigned long LID_OPEN_TIME = 4000;
const unsigned long TEMP_READ_INTERVAL = 60000;
const unsigned long BUZZER_DURATION = 2000;
const unsigned long MOTION_COOLDOWN = 5000;

// === State Variables ===
unsigned long lastTempReadTime = 0;
bool lidOpen = false;
unsigned long lidOpenTime = 0;
bool tempAlarmActive = false;
unsigned long buzzerStartTime = 0;
unsigned long lastMotionTime = 0;
bool motionDetectionEnabled = true;  // Changed from motionCooldown (inverted logic)
String receivedCommand = "";
float latestTemp = 0.0;
float latestHum = 0.0;
bool trashIsFull = false;

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);

  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  lidServo.attach(SERVO_PIN);
  closeLid();

  Serial.println("Smart Dustbin Ready");
}

void loop() {
  // === Handle incoming commands from ESP ===
  if (espSerial.available()) {
    char c = espSerial.read();
    if (c == '\n') {
      processESPCommand(receivedCommand);
      receivedCommand = "";
    } else {
      receivedCommand += c;
    }
  }

  // === PIR Motion with Cooldown ===
  if (!motionDetectionEnabled && (millis() - lastMotionTime >= MOTION_COOLDOWN)) {
    motionDetectionEnabled = true;
    Serial.println("Motion detection re-enabled");
  }

  if (!trashIsFull && motionDetectionEnabled && digitalRead(PIR_PIN) == HIGH) {
    Serial.println("Motion detected!");
    lastMotionTime = millis();
    motionDetectionEnabled = false;
    measureTrashLevel();
  }

  // === Auto-close Lid ===
  if (lidOpen && (millis() - lidOpenTime >= LID_OPEN_TIME)) {
    closeLid();
  }

  // === Periodic Temp/Humidity Reading ===
  if (millis() - lastTempReadTime >= TEMP_READ_INTERVAL) {
    readTemperatureHumidity();
    lastTempReadTime = millis();
  }

  // === Buzzer Timeout ===
  if (tempAlarmActive && millis() - buzzerStartTime >= BUZZER_DURATION) {
    noTone(BUZZER_PIN);
    tempAlarmActive = false;
  }
}

// === Command from ESP: Open Lid via RFID ===
void processESPCommand(String command) {
  command.trim();
  if (command == "OPEN_LID") {
    Serial.println("RFID authorized - Opening lid");
    openLid(true);  // bypass full check
    // Reset trashIsFull when RFID is used (assuming trash was emptied)
    trashIsFull = false;
    Serial.println("Trash full status reset - assuming trash was emptied");
  }
}

// === Trash Level Detection + Condition ===
void measureTrashLevel() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  int distance = duration * 0.034 / 2;

  Serial.print("Trash level: ");
  Serial.print(distance);
  Serial.println(" cm");

  if (distance <= TRASH_FULL_DISTANCE && distance > 0) {
    if (!trashIsFull) {  // Only send notification once
      trashIsFull = true;
      Serial.println("Trash is full! Sending TRASH_FULL to ESP...");
      espSerial.print("TRASH_FULL,");
      espSerial.print(latestTemp, 1);
      espSerial.print(",");
      espSerial.println(latestHum, 1);
    }
  } else {
    trashIsFull = false;
    openLid(false);  // try to open normally
  }
}

// === Lid Open Control ===
void openLid(bool overrideRFID) {
  if (!lidOpen) {
    if (trashIsFull && !overrideRFID) {
      Serial.println("Lid blocked: Trash is full and RFID not authorized.");
      return;
    }

    lidServo.write(0);  // OPEN position
    lidOpen = true;
    lidOpenTime = millis();
    Serial.println("Lid opened");

    if (overrideRFID || !trashIsFull) {
      espSerial.print("LID_OPENED,");
      espSerial.print(latestTemp, 1);
      espSerial.print(",");
      espSerial.println(latestHum, 1);
    }
  }
}

// === Lid Close ===
void closeLid() {
  lidServo.write(90);  // CLOSED position
  lidOpen = false;
  Serial.println("Lid closed");
}

// === DHT11 Read + Alarm ===
void readTemperatureHumidity() {
  int chk = DHT11.read(DHT_PIN);
  latestTemp = DHT11.temperature;
  latestHum = DHT11.humidity;

  Serial.print("Temperature: ");
  Serial.print(latestTemp, 1);
  Serial.print(" °C\tHumidity: ");
  Serial.print(latestHum, 1);
  Serial.println(" %");

  if (latestTemp > TEMP_ALARM_THRESHOLD) {
    triggerTemperatureAlarm();
  }
}

// === Buzzer Alarm ===
void triggerTemperatureAlarm() {
  Serial.println("High temperature alarm!");
  tone(BUZZER_PIN, 1000);
  tempAlarmActive = true;
  buzzerStartTime = millis();
}
 