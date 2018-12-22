#include <math.h>

// Configure these to adjust the behavior
const bool debug = true;
const int lightCheckDelaySeconds = 15; // How long to wait between checking the light sensor (default: 30 seconds)
const int consecutiveLightCheckMinutes = 15; // How long must all the light checks agree before the light state can change (default: 30 minutes)
const int doorRunTimeSeconds = 30; // How long should the door motor run (default: 30 seconds)
const int lightStateThreshold = 500; // How light must it get to change to the LIGHT state (default: 500)
const int darkStateThreshold = 200; // How dark must it get to change to the DARK state (default: 100)

// Digital pins
const int buttonPin = 2;
const int doorPin = 8;
const int speakerPin = 9;
const int greenLedPin = 12;
const int yellowLedPin = 11;
const int redLedPin = 10;

// Analog pins
const int lightPin = 0;

// Door variables
bool doorRunning = false;
unsigned long doorRunningStartTime = 0;
const int doorRunTime = doorRunTimeSeconds * 1000; // How long should the door motor run (in milliseconds)
enum LED {GREEN, YELLOW, RED};
const int minDoorWaitTimeHours = 10; // How long must the door keep a state before it can change states?
const unsigned long minDoorWaitTime = minDoorWaitTimeHours * 60 * 60 * 1000 // Convert the hours to milliseconds (3,600,000 per hour)
unsigned long lastDoorMotorRunTime = 0; // How long has it been since we last triggered the door motor to open or close

// Define the light states
enum LightState {LIGHT, DARK, NEUTRAL};
const String getLightStateName(enum LightState light) {
   switch (light) {
      case LIGHT: return "Light";
      case DARK: return "Dark";
      case NEUTRAL: return "Neutral";
   }
}

// Light detection variables
enum LightState currentLightState = NEUTRAL;
unsigned long lastLightCheck = 0;
const int lightCheckDelay = lightCheckDelaySeconds * 1000;
const int newStateThreshold = ceil((consecutiveLightCheckMinutes * 60000) / lightCheckDelay); // How many times must the new state be the same before the state can change
int newStateCount = 0;

// put your setup code here, to run once:
void setup() {  
  Serial.begin(9600);
  pinMode(doorPin, OUTPUT);
  pinMode(buttonPin, INPUT);
  pinMode(greenLedPin, OUTPUT);
  pinMode(yellowLedPin, OUTPUT);
  pinMode(redLedPin, OUTPUT);
  
  Serial.println("New state threshold: " + String(newStateThreshold));

  // Sample the current light and set the appropriate light state
  int lightAverage = 5;
  int lightValues = 0;
  for (int i = 0; i < lightAverage; i++) {
    lightValues += readLightSensor();
  }
  currentLightState = getLightState(lightValues / lightAverage);
  if (debug) {
    Serial.println("Found an average light value of " + String(lightValues / lightAverage) + ". Set light state to " + getLightStateName(currentLightState));
  }
  lightLed(GREEN);
}

// put your main code here, to run repeatedly:
void loop() {
  if (!doorRunning) {
    unsigned long currentTime = millis();
    // If it's time to check the light level again, grab the current level, then state, and figure out if we're transitioning to a new state
    if (currentTime - lastLightCheck >= lightCheckDelay && currentTime - lastDoorMotorRunTime >= minDoorWaitTime) {
      int lightLevel = readLightSensor();
      enum LightState newLightState = getLightState(lightLevel);
      if (newLightState != NEUTRAL && newLightState != currentLightState) {
        newStateCount++;
        // Play a 1 second tone to alert that the door state may soon change
        tone(speakerPin, 1000, 1000);
        lightLed(YELLOW);
      } else {
        newStateCount = 0;
        lightLed(GREEN);
      }
      Serial.println("New state count: " + String(newStateCount) + " (" + getLightStateName(newLightState) + " vs " + getLightStateName(currentLightState) + ")");
      
      // Check if we've made it to the new state threshold
      if (newStateCount >= newStateThreshold) {
        Serial.println("Switching state from " + getLightStateName(currentLightState) + " to " + getLightStateName(newLightState));
        currentLightState = newLightState;
        newStateCount = 0;
        lastDoorMotorRunTime = currentTime; // Restart the door wait countdown
        startDoorMotor();
      }
      lastLightCheck = currentTime;
    }
    
    if (digitalRead(buttonPin) == LOW) {
      if (debug) {
        Serial.println("Button pressed."); 
      }
      // If the door is manually opened, reset the light state to whatever the light level currently is
      currentLightState = getLightState(readLightSensor());
      newStateCount = 0;
      lastDoorMotorRunTime = currentTime; // Restart the door wait countdown
      startDoorMotor();
    }
  }
  
  if (doorRunning && isDoorMotorDone()) {
    stopDoorMotor();
  }
}

// We only want state changes at the light value extremes (LIGHT/DARK) to trigger the door motor. Otherwise, return a NEUTRAL state.
enum LightState getLightState(int lightValue) {
  if (lightValue <= darkStateThreshold) {
    return DARK;
  } else if (lightValue >= lightStateThreshold) { 
    return LIGHT;
  } else {
    return NEUTRAL;
  }
}

int readLightSensor() {
  // Grab the light sensor's value and invert it so dark is 0 and light is 1023
  int lightLevel = 1023 - analogRead(lightPin);
  if (debug) {
    Serial.println(String(lightLevel) + " (" + getLightStateName(getLightState(lightLevel)) + ")");
  }
  return lightLevel;
}

// Turn the door's power on for 30 seconds, then cycle it off
void startDoorMotor() {
  if (debug) {
    Serial.println("Turning door motor on.");
  }
  lightLed(RED);
  playSuccessTone();
  doorRunning = true;
  doorRunningStartTime = millis();
  digitalWrite(doorPin, HIGH);
}

bool isDoorMotorDone() {
  if (doorRunning && millis() - doorRunningStartTime >= doorRunTime) {
    return true;
  }
  return false;
}

void stopDoorMotor() {
  digitalWrite(doorPin, LOW);
  if (debug) {
    Serial.println("Waited " + String((millis() - doorRunningStartTime) / 1000) + " seconds.");
    Serial.println("Turning door motor off.");
  }
  doorRunning = false;
  lightLed(GREEN);
}

void lightLed(enum LED led) {
  digitalWrite(greenLedPin, LOW);
  digitalWrite(yellowLedPin, LOW);
  digitalWrite(redLedPin, LOW);
  
  switch (led) {
    case GREEN:
      digitalWrite(greenLedPin, HIGH);
      break;
    case YELLOW:
      digitalWrite(yellowLedPin, HIGH);
      break;
    case RED:
      digitalWrite(redLedPin, HIGH);
      break;
  }
}

void playSuccessTone() {
  tone(speakerPin, 1001, 250);
  delay(250);
  tone(speakerPin, 1063, 250);
  delay(250);
  tone(speakerPin, 1126, 250);
  delay(250);
  tone(speakerPin, 1194, 500);
  delay(500);
}
