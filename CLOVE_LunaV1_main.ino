
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

Preferences preferences;
// =====================================================
// TRESHOLD CONTAINER
// =====================================================
int openValues[5] = {0, 0, 0, 0, 0};
int thresholds[5] = {800, 800, 800, 800, 800};

const int BEND_DELTA = 150;
bool calibrationLoaded = false;

// =====================================================
// CLOVE BLUETOOTH CONFIGURATION
// =====================================================

BLECharacteristic *characteristic;

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

#define BOOT_BUTTON 0

bool connected = false;

// =====================================================
// EXISTING BUTTON VARIABLES
// =====================================================

unsigned long pressStartTime = 0;
const unsigned long Hold_Duration = 2000;
bool isPressing = false;

// =====================================================
// FLEX SENSOR PINS
// =====================================================

const int flex1 = 25; // Thumb
const int flex2 = 32; // Index
const int flex3 = 33; // Middle
const int flex4 = 34; // Ring
const int flex5 = 35; // Pinky

// =====================================================
// SENSOR THRESHOLD
// =====================================================

int threshold = 800;

// Latest sensor readings
int s1 = 0;
int s2 = 0;
int s3 = 0;
int s4 = 0;
int s5 = 0;

// =====================================================
// GESTURE VARIABLES
// =====================================================

String message = "";
String lastMessage = "";

// =====================================================
// RAW DATA TRANSMISSION TIMER
// =====================================================

unsigned long lastRawSend = 0;
const unsigned long RAW_SEND_INTERVAL = 250;
bool rawReadingsEnabled = true;

// =====================================================
// BLUETOOTH CALLBACKS
// =====================================================

class MyServerCallbacks : public BLEServerCallbacks {

  void onConnect(BLEServer* server) override {
    connected = true;
    lastMessage = "";

    Serial.println("Bluetooth connected!");
  }

  void onDisconnect(BLEServer* server) override {
    connected = false;
    lastMessage = "";

    Serial.println("Bluetooth disconnected!");

    server->startAdvertising();
  }
};

void loadCalibration() {
  preferences.begin("clove", true);

  calibrationLoaded = preferences.getBool("calibrated", false);

  if (calibrationLoaded) {
    for (int i = 0; i < 5; i++) {
      String key = "open" + String(i);
      openValues[i] = preferences.getInt(key.c_str(), 0);
      thresholds[i] = openValues[i] + BEND_DELTA;
    }
  }

  preferences.end();

  if (calibrationLoaded) {
    Serial.println("Saved calibration loaded:");
    for (int i = 0; i < 5; i++) {
      Serial.printf(
        "Finger %d: open=%d, threshold=%d\n",
        i + 1, openValues[i], thresholds[i]
      );
    }
  } else {
    Serial.println("No saved calibration. Using default thresholds.");
  }
}

// =====================================================
// Save Calibration 
// =====================================================
bool saveCalibrationCommand(String command) {
  if (!command.startsWith("CAL,")) {
    return false;
  }

  int values[5];
  int start = 4;

  for (int i = 0; i < 5; i++) {
    int comma = command.indexOf(',', start);

    String part;

    if (i < 4) {
      if (comma == -1) return false;
      part = command.substring(start, comma);
      start = comma + 1;
    } else {
      if (comma != -1) return false;
      part = command.substring(start);
    }

    part.trim();

    if (part.length() == 0) return false;

    for (int j = 0; j < part.length(); j++) {
      if (!isDigit(part[j])) return false;
    }

    long value = part.toInt();

    if (value < 0 || value > 4095) return false;

    values[i] = (int)value;
  }

  preferences.begin("clove", false);

  for (int i = 0; i < 5; i++) {
    openValues[i] = values[i];
    thresholds[i] = openValues[i] + BEND_DELTA;

    String key = "open" + String(i);
    preferences.putInt(key.c_str(), openValues[i]);
  }

  preferences.putBool("calibrated", true);
  preferences.end();

  calibrationLoaded = true;

  Serial.println("Calibration saved to ESP32 flash!");

  for (int i = 0; i < 5; i++) {
    Serial.printf(
      "Finger %d: open=%d, threshold=%d\n",
      i + 1, openValues[i], thresholds[i]
    );
  }

  return true;
}

// =====================================================
// RECEIVE COMMANDS FROM CLOVE APP
// =====================================================

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) override {

    String command = pCharacteristic->getValue().c_str();
    command.trim();

    if (command == "RAW_OFF") {
      rawReadingsEnabled = false;
      Serial.println("Raw sensor updates PAUSED");
    }
    else if (command == "RAW_ON") {
      rawReadingsEnabled = true;
      Serial.println("Raw sensor updates ENABLED");
    }
    else if (command.startsWith("CAL,")) {
      if (saveCalibrationCommand(command)) {
        Serial.println("CALIBRATION RECEIVED SUCCESSFULLY");
      } else {
        Serial.println("Invalid calibration command!");
      }
    }
  }
};

// =====================================================
// READ ALL FIVE SENSORS
// =====================================================

void readSensors() {
  s1 = analogRead(flex1);
  s2 = analogRead(flex2);
  s3 = analogRead(flex3);
  s4 = analogRead(flex4);
  s5 = analogRead(flex5);
}

// =====================================================
// PRINT SENSOR READINGS TO SERIAL MONITOR
// =====================================================

void printSensorReadings() {
  Serial.print("Thumb: ");
  Serial.print(s1);

  Serial.print(" | Index: ");
  Serial.print(s2);

  Serial.print(" | Middle: ");
  Serial.print(s3);

  Serial.print(" | Ring: ");
  Serial.print(s4);

  Serial.print(" | Pinky: ");
  Serial.println(s5);
}

// =====================================================
// SEND RAW READINGS THROUGH BLUETOOTH
//
// Format:
// RAW,thumb,index,middle,ring,pinky
//
// Example:
// RAW, 520, 610, 480, 700, 550
// =====================================================

void sendRawReadings() {

  String rawData =
      "RAW," +
      String(s1) + "," +
      String(s2) + "," +
      String(s3) + "," +
      String(s4) + "," +
      String(s5);

  characteristic->setValue(rawData.c_str());
  characteristic->notify();

  Serial.print("RAW SENT: ");
  Serial.println(rawData);
}

// =====================================================
// DETECT AND SEND GESTURE CODES
// =====================================================

void detectGesture() {

  // Determine which fingers are bent
  bool b1 = s1 > thresholds[0];
  bool b2 = s2 > thresholds[1];
  bool b3 = s3 > thresholds[2];
  bool b4 = s4 > thresholds[3];
  bool b5 = s5 > thresholds[4];
  
  // Encode finger states into a bitmask
  int code = 0;

  if (b1) code |= 1 << 0;
  if (b2) code |= 1 << 1;
  if (b3) code |= 1 << 2;
  if (b4) code |= 1 << 3;
  if (b5) code |= 1 << 4;

  message = "";

  // Preserve all original gesture mappings
  switch (code) {

    case 0b11111:
      message = "1";
      break;

    case 0b11110:
      message = "2";
      break;

    case 0b01111:
      message = "3";
      break;

    case 0b00111:
      message = "4";
      break;

    case 0b11100:
      message = "5";
      break;

    case 0b00011:
      message = "6";
      break;

    case 0b00110:
      message = "7";
      break;

    case 0b00001:
      message = "8";
      break;

    case 0b00010:
      message = "9";
      break;

    case 0b00100:
      message = "10";
      break;

    case 0b01000:
      message = "11";
      break;

    case 0b10000:
      message = "12";
      break;

    default:
      message = "";
      break;
  }

  // Send a gesture only when it changes
  if (connected &&
      !message.isEmpty() &&
      message != lastMessage) {

    characteristic->setValue(message.c_str());
    characteristic->notify();

    Serial.print("GESTURE SENT: ");
    Serial.println(message);

    lastMessage = message;
  }

  // Permit the same gesture to trigger again after
  // the hand returns to an unrecognized position.
  if (message.isEmpty()) {
    lastMessage = "";
  }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  //load treshold from database
  loadCalibration();

  // ESP32 ADC configuration
  analogReadResolution(12);

  // Start Bluetooth using your existing device name
  BLEDevice::init("CLOVE");

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());

  BLEService *service =
      server->createService(SERVICE_UUID);

  characteristic = service->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
      BLECharacteristic::PROPERTY_WRITE |
      BLECharacteristic::PROPERTY_NOTIFY
  );

  characteristic->addDescriptor(new BLE2902());

  characteristic->setCallbacks(new MyCharacteristicCallbacks());

  characteristic->setValue("Ready");

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();

  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);

  BLEDevice::startAdvertising();

  Serial.println("================================");
  Serial.println("CLOVE BLE STARTED");
  Serial.println("Device name: CLOVE");
  Serial.println("Threshold: 800");
  Serial.println("Raw sensor updates: 250 ms");
  Serial.println("Gesture codes: 1 to 12");
  Serial.println("================================");
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  // Read all five sensors
  readSensors();

  // Display values in Arduino Serial Monitor
  printSensorReadings();

  // Preserve gesture recognition
  detectGesture();

  // Send raw readings periodically when connected
  unsigned long currentTime = millis();

 if (connected &&
    rawReadingsEnabled &&
    currentTime - lastRawSend >= RAW_SEND_INTERVAL) {

  lastRawSend = currentTime;
  sendRawReadings();
}
  // Short delay instead of the original 2000 ms
  delay(500);
}
