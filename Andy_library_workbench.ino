#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <VolvoDIM.h>

// SD card chip-select pin (for Seeed CAN Bus shield, typically 4)
const int chipSelect = 4;

// Create a global gauge instance (SPI CS pin: 9, Relay: 6)
VolvoDIM gauge(9, 6);

// Global flag for replay state.
bool replayActive = false;

// If your environment does not supply FlowSerialReadStringUntil, define it:
String FlowSerialReadStringUntil(char terminator) {
  return Serial.readStringUntil(terminator);
}

// ---------------- Global Variables for Custom Injection Modes ----------------

// For Unique Loop Mode (non-blocking)
bool uniqueLoopModeActive = false;
unsigned long uniqueLoopCanId = 0;
byte uniqueLoopData[8] = {0, 0, 0, 0, 0, 0, 0, 0};
unsigned long lastUniqueLoopTime = 0;
const unsigned long uniqueLoopInterval = 15;  // send every 15 ms

// For Range Test Mode (non-blocking)
bool rangeTestActive = false;
unsigned long rangeTestCanId = 0;
int rangeTestByteIndex = 0;
int rangeTestCurrentValue = 0;
unsigned long lastRangeTestTime = 0;
const unsigned long rangeTestInterval = 500;  // one message per second

// ---------------- Helper Functions ----------------

void updateUniqueLoop() {
  if (uniqueLoopModeActive && (millis() - lastUniqueLoopTime >= uniqueLoopInterval)) {
    gauge.sendCANMessage(uniqueLoopCanId, uniqueLoopData);
    lastUniqueLoopTime = millis();
  }
}

void updateRangeTest() {
  if (rangeTestActive && (millis() - lastRangeTestTime >= rangeTestInterval)) {
    byte data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    data[rangeTestByteIndex] = (byte)rangeTestCurrentValue;
    gauge.sendCANMessage(rangeTestCanId, data);
    
    Serial.print(F("Range Test on Byte "));
    Serial.print(rangeTestByteIndex);
    Serial.print(F(": Sent CAN ID 0x"));
    Serial.print(rangeTestCanId, HEX);
    Serial.print(F(", {"));
    for (int i = 0; i < 8; i++) {
      Serial.print(F("0x"));
      if (data[i] < 16) Serial.print(F("0"));
      Serial.print(data[i], HEX);
      if (i < 7) Serial.print(F(", "));
    }
    Serial.println(F("}"));
    
    rangeTestCurrentValue++;
    if (rangeTestCurrentValue > 255) {
      rangeTestActive = false;
      Serial.println(F("Range test complete."));
    }
    lastRangeTestTime = millis();
  }
}

// ---------------- File Replay Function ----------------
// This function replays a CAN bus log file from the SD card.
// The file is expected to be in the root folder of the SD card and follow a format like:
//   Time   ID     DLC Data                    Comment
//   20.642 00E01008 8 03 61 8C 00 00 28 4C 00
// For each line (after skipping any header), it parses the timestamp, CAN ID,
// DLC and up to 8 data bytes, delays according to the timestamp difference,
// sends the CAN message, and prints the replayed message.
void replaySniffFile(const char* filename) {
  replayActive = true;  // Pause simulation/injections
  File dataFile = SD.open(filename);
  if (!dataFile) {
    Serial.print(F("Error opening file: "));
    Serial.println(filename);
    replayActive = false;
    return;
  }
  
  Serial.print(F("Replaying file: "));
  Serial.println(filename);
  
  float prevTime = 0;
  bool firstMessage = true;
  
  while (dataFile.available()) {
    String line = dataFile.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (line.startsWith(F("Time")) || line.startsWith(F("ID")))
      continue;
    
    int index = 0;
    int spaceIndex = line.indexOf(' ');
    if (spaceIndex == -1) continue;
    String timeStr = line.substring(0, spaceIndex);
    float curTime = timeStr.toFloat();
    
    index = spaceIndex + 1;
    spaceIndex = line.indexOf(' ', index);
    if (spaceIndex == -1) continue;
    String canIdStr = line.substring(index, spaceIndex);
    unsigned long canId = strtoul(canIdStr.c_str(), NULL, 16);
    
    index = spaceIndex + 1;
    spaceIndex = line.indexOf(' ', index);
    if (spaceIndex == -1) continue;
    String dlcStr = line.substring(index, spaceIndex);
    int dlc = dlcStr.toInt();
    
    byte data[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < dlc && i < 8; i++) {
      index = spaceIndex + 1;
      spaceIndex = line.indexOf(' ', index);
      String byteStr;
      if (spaceIndex == -1) {
        byteStr = line.substring(index);
      } else {
        byteStr = line.substring(index, spaceIndex);
      }
      byteStr.trim();
      data[i] = (byte) strtoul(byteStr.c_str(), NULL, 16);
    }
    
    if (!firstMessage) {
      float delayTime = (curTime - prevTime) * 1000; // in ms
      if (delayTime < 0) delayTime = 0;
      delay((unsigned long)delayTime);
    } else {
      firstMessage = false;
    }
    prevTime = curTime;
    
    gauge.sendCANMessage(canId, data);
    Serial.print(F("Replayed Message: 0x"));
    Serial.print(canId, HEX);
    Serial.print(F(", {"));
    for (int i = 0; i < 8; i++) {
      Serial.print(F("0x"));
      if (data[i] < 16) Serial.print(F("0"));
      Serial.print(data[i], HEX);
      if (i < 7) Serial.print(F(", "));
    }
    Serial.println(F("}"));
  }
  
  dataFile.close();
  Serial.println(F("Replay complete."));
  replayActive = false;  // Resume simulation/injections
}

// ---------------- Command Processing ----------------
// Supported commands (each ending with a newline):
//
// (A) Direct single message command:
//     0x3600028, {0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00}
//
// (B) Unique Message Commands (SINGLE/LOOP):
//     SINGLE,0x3600028,00 00 00 00 00 00 00 00
//     LOOP,0x3600028,00 00 00 00 00 00 00 00
//
// (C) Range Test (non-blocking):
//     RANGE,0x3600028,2
//
// (D) Direct Function Commands (e.g., SPEED, RPM, etc.)
//
// (E) Replay Command: "REPLAY" or "REPLAY,filename.trc"
void processSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  
  // (E) If command starts with "REPLAY,"
  if (cmd.startsWith(F("REPLAY,"))) {
    int commaIndex = cmd.indexOf(F(","));
    if (commaIndex >= 0) {
      String filename = cmd.substring(commaIndex + 1);
      filename.trim();
      replaySniffFile(filename.c_str());
      return;
    }
  }
  
  // If command equals "REPLAY", use default file.
  if (cmd.equalsIgnoreCase(F("REPLAY"))) {
    replaySniffFile("sniff2.trc");
    return;
  }
  
  // While replay is active, ignore all commands except STOP.
  if (replayActive && !cmd.equalsIgnoreCase(F("STOP"))) {
    Serial.println(F("Replay in progress; please wait or type STOP to cancel."));
    return;
  }
  
  if (cmd.equalsIgnoreCase(F("STOP"))) {
    uniqueLoopModeActive = false;
    rangeTestActive = false;
    Serial.println(F("Stopped any active injection mode."));
    return;
  }
  
  // (A) If command starts with "0x", treat as a direct single message command.
  if (cmd.startsWith(F("0x"))) {
    int commaIndex = cmd.indexOf(F(","));
    if (commaIndex < 0) return;
    String canIdStr = cmd.substring(0, commaIndex);
    unsigned long canId = strtoul(canIdStr.c_str(), NULL, 0);
    
    int braceOpen = cmd.indexOf(F("{"));
    int braceClose = cmd.indexOf(F("}"));
    if (braceOpen < 0 || braceClose < 0) return;
    String dataContent = cmd.substring(braceOpen + 1, braceClose);
    
    byte data[8];
    int dataIndex = 0;
    int start = 0;
    while (dataIndex < 8) {
      int commaPos = dataContent.indexOf(F(","), start);
      String byteStr;
      if (commaPos == -1) {
        byteStr = dataContent.substring(start);
        byteStr.trim();
        if (byteStr.startsWith(F("0x")) || byteStr.startsWith(F("0X")))
          byteStr = byteStr.substring(2);
        data[dataIndex++] = (byte) strtoul(byteStr.c_str(), NULL, 16);
        break;
      } else {
        byteStr = dataContent.substring(start, commaPos);
        byteStr.trim();
        if (byteStr.startsWith(F("0x")) || byteStr.startsWith(F("0X")))
          byteStr = byteStr.substring(2);
        data[dataIndex++] = (byte) strtoul(byteStr.c_str(), NULL, 16);
        start = commaPos + 1;
      }
    }
    
    gauge.sendCANMessage(canId, data);
    Serial.print(F("Single Message "));
    Serial.print(F("0x"));
    Serial.print(canId, HEX);
    Serial.print(F(", {"));
    for (int i = 0; i < 8; i++) {
      Serial.print(F("0x"));
      if (data[i] < 16) Serial.print(F("0"));
      Serial.print(data[i], HEX);
      if (i < 7) Serial.print(F(", "));
    }
    Serial.println(F("} sent"));
    return;
  }
  
  // (B), (C), (D): Process commands with a leading keyword.
  int firstComma = cmd.indexOf(F(","));
  if (firstComma < 0) return;
  String cmdType = cmd.substring(0, firstComma);
  
  if (cmdType.equalsIgnoreCase(F("SINGLE")) || cmdType.equalsIgnoreCase(F("LOOP"))) {
    bool loopMode = cmdType.equalsIgnoreCase(F("LOOP"));
    int secondComma = cmd.indexOf(F(","), firstComma + 1);
    if (secondComma < 0) return;
    String canIdStr = cmd.substring(firstComma + 1, secondComma);
    unsigned long canId = strtoul(canIdStr.c_str(), NULL, 0);
    
    String dataStr = cmd.substring(secondComma + 1);
    byte data[8];
    int dataIndex = 0;
    int start = 0;
    while (dataIndex < 8 && start >= 0) {
      int end = dataStr.indexOf(F(" "), start);
      String byteStr;
      if (end == -1) {
        byteStr = dataStr.substring(start);
        start = -1;
      } else {
        byteStr = dataStr.substring(start, end);
        start = end + 1;
      }
      data[dataIndex++] = (byte) strtoul(byteStr.c_str(), NULL, 16);
    }
    
    if (loopMode) {
      uniqueLoopModeActive = true;
      uniqueLoopCanId = canId;
      for (int i = 0; i < 8; i++) {
        uniqueLoopData[i] = data[i];
      }
      lastUniqueLoopTime = millis();
      Serial.println(F("Unique LOOP mode activated. Type 'STOP' to exit."));
    } else {
      gauge.sendCANMessage(canId, data);
      Serial.print(F("Single Message "));
      Serial.print(F("0x"));
      Serial.print(canId, HEX);
      Serial.print(F(", {"));
      for (int i = 0; i < 8; i++) {
        Serial.print(F("0x"));
        if (data[i] < 16) Serial.print(F("0"));
        Serial.print(data[i], HEX);
        if (i < 7) Serial.print(F(", "));
      }
      Serial.println(F("} sent"));
    }
  }
  else if (cmdType.equalsIgnoreCase(F("RANGE"))) {
    int secondComma = cmd.indexOf(F(","), firstComma + 1);
    if (secondComma < 0) return;
    String canIdStr = cmd.substring(firstComma + 1, secondComma);
    unsigned long canId = strtoul(canIdStr.c_str(), NULL, 0);
    String byteIndexStr = cmd.substring(secondComma + 1);
    int byteIndex = byteIndexStr.toInt();
    
    rangeTestActive = true;
    rangeTestCanId = canId;
    rangeTestByteIndex = byteIndex;
    rangeTestCurrentValue = 0;
    lastRangeTestTime = millis();
    Serial.println(F("Range test activated (one message per second). Type 'STOP' to exit."));
  }
  else if (cmdType.equalsIgnoreCase(F("SPEED"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setSpeed(val);
    Serial.print(F("Called setSpeed("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("RPM"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setRpm(val);
    Serial.print(F("Called setRpm("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("GAS"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setGasLevel(val);
    Serial.print(F("Called setGasLevel("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("COOLTEMP"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setOutdoorTemp(val);
    Serial.print(F("Called setOutdoorTemp("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("COOLANT"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setCoolantTemp(val);
    Serial.print(F("Called setCoolantTemp("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("GEAR"))) {
    String gearVal = cmd.substring(firstComma + 1);
    gearVal.trim();
    if (gearVal.length() > 0) {
      gauge.setGearPosText(gearVal.c_str());
      Serial.print(F("Called setGearPosText("));
      Serial.print(gearVal);
      Serial.println(F(")"));
    }
  }
  else if (cmdType.equalsIgnoreCase(F("LEFTBLINKER"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setLeftBlinkerSolid(val);
    Serial.print(F("Called setLeftBlinkerSolid("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("RIGHTBLINKER"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setRightBlinkerSolid(val);
    Serial.print(F("Called setRightBlinkerSolid("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("TIME"))) {
    int secondComma = cmd.indexOf(F(","), firstComma + 1);
    if (secondComma < 0) return;
    int hour = cmd.substring(firstComma + 1, secondComma).toInt();
    int minute = cmd.substring(secondComma + 1).toInt();
    int timeVal = gauge.clockToDecimal(hour, minute, 1);
    gauge.setTime(timeVal);
    Serial.print(F("Called setTime("));
    Serial.print(timeVal);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("MILEAGE"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableMilageTracking(val);
    Serial.print(F("Called enableMilageTracking("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("DING"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableDisableDingNoise(val);
    Serial.print(F("Called enableDisableDingNoise("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("BRIGHTNESS"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setTotalBrightness(val);
    Serial.print(F("Called setTotalBrightness("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("HIGHBEAM"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableHighBeam(val);
    Serial.print(F("Called enableHighBeam("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("FOG"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableFog(val);
    Serial.print(F("Called enableFog("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else if (cmdType.equalsIgnoreCase(F("BRAKE"))) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableBrake(val);
    Serial.print(F("Called enableBrake("));
    Serial.print(val);
    Serial.println(F(")"));
  }
  else {
    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  if (!SD.begin(chipSelect)) {
    Serial.println(F("SD card initialization failed!"));
  } else {
    Serial.println(F("SD card initialized."));
  }
  
  gauge.init();
  Serial.println(F("Gauge initialized and powered on."));
}

void loop() {
  // Only run simulation and injection updates if not replaying.
  if (!replayActive) {
    gauge.simulate();
    updateUniqueLoop();
    updateRangeTest();
  }
  
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    processSerialCommand(command);
  }
  
  delay(15);
}
