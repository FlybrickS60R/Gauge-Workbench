//SHCustomProtocol in Simhub's testbench

#ifndef __SHCUSTOMPROTOCOL_H__
#define __SHCUSTOMPROTOCOL_H__

#include <Arduino.h>
#include <VolvoDIM.h>

VolvoDIM VolvoDIM(9, 6);
int totalBlink = 0, innerLeftBlink = 0, innerRightBlink = 0;
int LeftTurn = 0;
int RightTurn = 0;

// Global variables for debounce logic and state management
unsigned long previousMillisLeft = 0;
unsigned long previousMillisRight = 0;
const long debounceDelay = 200;  // debounce delay in milliseconds
const long blinkInterval = 500;  // blink interval in milliseconds

class SHCustomProtocol {
private:
  bool leftBlinkerState = false;
  bool rightBlinkerState = false;
  unsigned long lastBlinkMillisLeft = 0;
  unsigned long lastBlinkMillisRight = 0;
  bool engineOn = false; // New variable to track engine state

  void handleBlinker(int currentTurn, bool &blinkerState, unsigned long &lastBlinkMillis, int &previousTurn, unsigned long &previousMillis, void (VolvoDIM::*setBlinker)(int)) {
    unsigned long currentMillis = millis();

    if (currentTurn != previousTurn) {
        previousMillis = currentMillis;
        previousTurn = currentTurn;
        blinkerState = (currentTurn == 1);
        lastBlinkMillis = currentMillis;
    }

    if (currentTurn == 1) {
        if (currentMillis - lastBlinkMillis >= blinkInterval) {
            blinkerState = !blinkerState;
            (VolvoDIM.*setBlinker)(blinkerState ? 1 : 0);
            lastBlinkMillis = currentMillis;
        }
    } else {
        blinkerState = false;
        (VolvoDIM.*setBlinker)(0);
    }
  }

public:

  void setup() {
    VolvoDIM.gaugeReset(); 
    VolvoDIM.init();
  }

  void read() {
      	int coolantTemp = floor(FlowSerialReadStringUntil(',').toInt() * .72);      //1
      	int carSpeed = FlowSerialReadStringUntil(',').toInt();                      //2
      	int rpms = FlowSerialReadStringUntil(',').toInt();                          //3
      	int fuelPercent = FlowSerialReadStringUntil(',').toInt();                   //4
      	int oilTemp = FlowSerialReadStringUntil(',').toInt();                       //5
      	String gear = FlowSerialReadStringUntil(',');                               //6
      	int currentLeftTurn = FlowSerialReadStringUntil(',').toInt();               //7
      	int currentRightTurn = FlowSerialReadStringUntil(',').toInt();              //8
      	int hour = FlowSerialReadStringUntil(',').toInt();                          //9
      	int minute = FlowSerialReadStringUntil(',').toInt();                        //10
      	int mileage = FlowSerialReadStringUntil(',').toInt();                       //11
      	int ding = FlowSerialReadStringUntil(',').toInt();                          //12
      	int totalBrightness = FlowSerialReadStringUntil(',').toInt();      	    //13
      	int highbeam = FlowSerialReadStringUntil(',').toInt();                      //14 
      	int fog = FlowSerialReadStringUntil(',').toInt();                           //15
      	int brake = FlowSerialReadStringUntil('\n').toInt();                         //16
   	


      int timeValue = VolvoDIM.clockToDecimal(hour, minute, 1);

      // Handle left blinker
      handleBlinker(currentLeftTurn, leftBlinkerState, lastBlinkMillisLeft, LeftTurn, previousMillisLeft, &VolvoDIM::setLeftBlinkerSolid);

      // Handle right blinker
      handleBlinker(currentRightTurn, rightBlinkerState, lastBlinkMillisRight, RightTurn, previousMillisRight, &VolvoDIM::setRightBlinkerSolid);

      // Update other gauges
	VolvoDIM.setTime(timeValue); 
	VolvoDIM.setOutdoorTemp(oilTemp);
	VolvoDIM.setCoolantTemp(coolantTemp);
	VolvoDIM.setSpeed(carSpeed);
	VolvoDIM.setGasLevel(fuelPercent);
	VolvoDIM.setRpm(rpms);
	VolvoDIM.setGearPosText(gear.charAt(0));
	VolvoDIM.enableMilageTracking(mileage);
	VolvoDIM.enableDisableDingNoise(ding);
	VolvoDIM.enableHighBeam(highbeam);
	VolvoDIM.setTotalBrightness(totalBrightness);
	VolvoDIM.enableFog(fog);
	VolvoDIM.enableBrake(brake);
      
  }

  void loop() {
    VolvoDIM.simulate();
  }

  void idle() {
  }
};

#endif
