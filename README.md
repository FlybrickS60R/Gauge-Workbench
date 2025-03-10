# Gauge-Workbench
Andy Gabler's VolvoDIM library reworked so that it can be used without Simhub for workbench testing and finding Can IDs and messages. This sketch  allows users to send one single Can Id and message, loop it or ask to test a range within a byte. 

Accepted commands:

Example

SINGLE,0x217FFC,00 00 00 00 00 00 00 00 - To test one single message

LOOP,0x217FFC,AA BB CC DD EE FF 11 22 - Loop one single message

RANGE,0x217FFC,2 - Test within this Can ID one byte in sequence.

SPEED,40  → calls setSpeed(40)

RPM,3000  → calls setRpm(3000)

GAS,50  → calls setGasLevel(50)

COOLTEMP,30 → calls setOutdoorTemp(30)

COOLANT,25 → calls setCoolantTemp(25)

GEAR,R   → calls setGearPosText("R")

LEFTBLINKER,1 → calls setLeftBlinkerSolid(1)

RIGHTBLINKER,1 → calls setRightBlinkerSolid(1)

TIME,12,30  → computes time using clockToDecimal(12,30,1) and calls setTime()

MILEAGE,1  → calls enableMilageTracking(1)

DING,1   → calls enableDisableDingNoise(1)

BRIGHTNESS,200 → calls setTotalBrightness(200)

HIGHBEAM,1  → calls enableHighBeam(1)

FOG,1    → calls enableFog(1)

BRAKE,1   → calls enableBrake(1)


