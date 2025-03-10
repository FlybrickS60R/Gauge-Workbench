#include <Arduino.h>
#include <SPI.h>
#include <VolvoDIM.h>

// Create a global gauge instance (SPI CS pin: 9, Relay: 6)
VolvoDIM gauge(9, 6);

// If your environment does not supply FlowSerialReadStringUntil, define it:
String FlowSerialReadStringUntil(char terminator) {
  return Serial.readStringUntil(terminator);
}

// ---------------- Global Variables for Custom Injection Modes ----------------

// For Unique Loop Mode (non-blocking)
bool uniqueLoopModeActive = false;
unsigned long uniqueLoopCanId = 0;
byte uniqueLoopData[8] = {0,0,0,0,0,0,0,0};
unsigned long lastUniqueLoopTime = 0;
const unsigned long uniqueLoopInterval = 15;  // send every 15ms

// For Range Test Mode (non-blocking)
bool rangeTestActive = false;
unsigned long rangeTestCanId = 0;
int rangeTestByteIndex = 0;
int rangeTestCurrentValue = 0;
unsigned long lastRangeTestTime = 0;
const unsigned long rangeTestInterval = 1000;  // one message per second

// ---------------- Helper Functions to Update Custom Modes ----------------

// Update unique loop mode if active
void updateUniqueLoop() {
  if (uniqueLoopModeActive && (millis() - lastUniqueLoopTime >= uniqueLoopInterval)) {
    gauge.sendCANMessage(uniqueLoopCanId, uniqueLoopData);
    lastUniqueLoopTime = millis();
  }
}

// Update range test mode if active
void updateRangeTest() {
  if (rangeTestActive && (millis() - lastRangeTestTime >= rangeTestInterval)) {
    byte data[8] = {0,0,0,0,0,0,0,0};
    data[rangeTestByteIndex] = (byte)rangeTestCurrentValue;
    gauge.sendCANMessage(rangeTestCanId, data);
    Serial.print("Range Test: Sent CAN ID 0x");
    Serial.print(rangeTestCanId, HEX);
    Serial.print(" with byte ");
    Serial.print(rangeTestByteIndex);
    Serial.print(" = 0x");
    if (rangeTestCurrentValue < 16) Serial.print("0");
    Serial.println(rangeTestCurrentValue, HEX);
    
    rangeTestCurrentValue++;
    if (rangeTestCurrentValue > 255) {
      rangeTestActive = false;
      Serial.println("Range test complete.");
    }
    lastRangeTestTime = millis();
  }
}

// ---------------- Command Processing ----------------
// This function processes commands (each ending with a newline).
// Supported command formats (one per line):
//
// UNIQUE MESSAGES:
//   SINGLE,<CAN_ID>,<8 hex bytes separated by spaces>
//   LOOP,<CAN_ID>,<8 hex bytes separated by spaces>
//
// RANGE TEST (non-blocking):
//   RANGE,<CAN_ID>,<byte_index>
//
// DIRECT FUNCTIONS:
//   SPEED,<value>
//   RPM,<value>
//   GAS,<value>
//   COOLTEMP,<value>      (calls setOutdoorTemp())
//   COOLANT,<value>       (calls setCoolantTemp())
//   GEAR,<text>           (calls setGearPosText)
//   LEFTBLINKER,<value>   (0 or 1)
//   RIGHTBLINKER,<value>  (0 or 1)
//   TIME,<hour>,<minute>  (AM assumed)
//   MILEAGE,<value>       (calls enableMilageTracking())
//   DING,<value>          (calls enableDisableDingNoise())
//   BRIGHTNESS,<value>    (calls setTotalBrightness())
//   HIGHBEAM,<value>      (calls enableHighBeam())
//   FOG,<value>           (calls enableFog())
//   BRAKE,<value>         (calls enableBrake())
//   STOP                  (to cancel any active LOOP or RANGE test)
void processSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  
  // If the command is STOP, disable any active injection mode.
  if (cmd.equalsIgnoreCase("STOP")) {
    uniqueLoopModeActive = false;
    rangeTestActive = false;
    Serial.println("Stopped any active injection mode.");
    return;
  }
  
  int firstComma = cmd.indexOf(',');
  if (firstComma < 0) return; // invalid command
  
  String cmdType = cmd.substring(0, firstComma);
  
  // ---------- Method 1: Unique Message Commands ----------
  if (cmdType.equalsIgnoreCase("SINGLE") || cmdType.equalsIgnoreCase("LOOP")) {
    bool loopMode = cmdType.equalsIgnoreCase("LOOP");
    int secondComma = cmd.indexOf(',', firstComma + 1);
    if (secondComma < 0) return;
    
    String canIdStr = cmd.substring(firstComma + 1, secondComma);
    unsigned long canId = strtoul(canIdStr.c_str(), NULL, 0);
    
    String dataStr = cmd.substring(secondComma + 1);
    byte data[8];
    int dataIndex = 0;
    int start = 0;
    while (dataIndex < 8 && start >= 0) {
      int end = dataStr.indexOf(' ', start);
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
      // Instead of a blocking loop, store the parameters for non-blocking update.
      uniqueLoopModeActive = true;
      uniqueLoopCanId = canId;
      for (int i = 0; i < 8; i++) {
        uniqueLoopData[i] = data[i];
      }
      lastUniqueLoopTime = millis();
      Serial.println("Unique LOOP mode activated. Type 'STOP' to exit.");
    } else {
      gauge.sendCANMessage(canId, data);
      Serial.println("Single unique CAN message sent.");
    }
  }
  // ---------- Method 2: Range Test Command (non-blocking) ----------
  else if (cmdType.equalsIgnoreCase("RANGE")) {
    int secondComma = cmd.indexOf(',', firstComma + 1);
    if (secondComma < 0) return;
    String canIdStr = cmd.substring(firstComma + 1, secondComma);
    unsigned long canId = strtoul(canIdStr.c_str(), NULL, 0);
    String byteIndexStr = cmd.substring(secondComma + 1);
    int byteIndex = byteIndexStr.toInt();
    
    // Initialize range test parameters
    rangeTestActive = true;
    rangeTestCanId = canId;
    rangeTestByteIndex = byteIndex;
    rangeTestCurrentValue = 0;
    lastRangeTestTime = millis();
    Serial.println("Range test activated (one message per second). Type 'STOP' to exit.");
  }
  // ---------- Method 3: Direct Function Commands ----------
  else if (cmdType.equalsIgnoreCase("SPEED")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setSpeed(val);
    Serial.print("Called setSpeed(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("RPM")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setRpm(val);
    Serial.print("Called setRpm(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("GAS")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setGasLevel(val);
    Serial.print("Called setGasLevel(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("COOLTEMP")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setOutdoorTemp(val);
    Serial.print("Called setOutdoorTemp(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("COOLANT")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setCoolantTemp(val);
    Serial.print("Called setCoolantTemp(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("GEAR")) {
    String gearVal = cmd.substring(firstComma + 1);
    gearVal.trim();
    if (gearVal.length() > 0) {
      gauge.setGearPosText(gearVal.c_str());
      Serial.print("Called setGearPosText(");
      Serial.print(gearVal);
      Serial.println(")");
    }
  }
  else if (cmdType.equalsIgnoreCase("LEFTBLINKER")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setLeftBlinkerSolid(val);
    Serial.print("Called setLeftBlinkerSolid(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("RIGHTBLINKER")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setRightBlinkerSolid(val);
    Serial.print("Called setRightBlinkerSolid(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("TIME")) {
    // Expecting format: TIME,<hour>,<minute>
    int secondComma = cmd.indexOf(',', firstComma + 1);
    if (secondComma < 0) return;
    int hour = cmd.substring(firstComma + 1, secondComma).toInt();
    int minute = cmd.substring(secondComma + 1).toInt();
    int timeVal = gauge.clockToDecimal(hour, minute, 1);
    gauge.setTime(timeVal);
    Serial.print("Called setTime(");
    Serial.print(timeVal);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("MILEAGE")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableMilageTracking(val);
    Serial.print("Called enableMilageTracking(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("DING")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableDisableDingNoise(val);
    Serial.print("Called enableDisableDingNoise(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("BRIGHTNESS")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.setTotalBrightness(val);
    Serial.print("Called setTotalBrightness(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("HIGHBEAM")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableHighBeam(val);
    Serial.print("Called enableHighBeam(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("FOG")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableFog(val);
    Serial.print("Called enableFog(");
    Serial.print(val);
    Serial.println(")");
  }
  else if (cmdType.equalsIgnoreCase("BRAKE")) {
    int val = cmd.substring(firstComma + 1).toInt();
    gauge.enableBrake(val);
    Serial.print("Called enableBrake(");
    Serial.print(val);
    Serial.println(")");
  }
  else {
    Serial.print("Unknown command: ");
    Serial.println(cmd);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // Allow time for Serial to initialize
  
  // Initialize the gauge (sets up the CAN bus and powers it on via relay pin 6)
  gauge.init();
  Serial.println("Gauge initialized and powered on.");
}

void loop() {
  // Always keep the gauge alive by continuously simulating its CAN messages.
  gauge.simulate();
  
  // Process incoming serial commands, if any.
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil('\n');
    processSerialCommand(command);
  }
  
  // Update non-blocking modes (if active)
  updateUniqueLoop();
  updateRangeTest();
  
  delay(15);  // Maintain regular loop timing
}
