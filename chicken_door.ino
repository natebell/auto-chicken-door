#include <math.h>
#include <Servo.h>

// Configure these to adjust the behavior
const bool debug = true;
const int lightCheckDelaySeconds = 15; // How long to wait between checking the light sensor (default: 30 seconds)
const int consecutiveLightCheckMinutes = 5; // How long must all the light checks agree before the light state can change (default: 30 minutes)
const int doorRunTimeSeconds = 20; // How long should the door motor run (default: 30 seconds)
const int lightStateThreshold = 550; // How light must it get to change to the LIGHT state (default: 500)
const int darkStateThreshold = 250; // How dark must it get to change to the DARK state (default: 100)
const int unlockDoorPos = 75;   // Min range of travel for lock's servo. Should unlock bolt while still allowing full travel of servo (default: 80)
const int lockDoorPos = 150; // Max range of travel for lock's servo. Should lock bolt while still allowing full travel of servo (default: 150)

// Digital pins
const int buttonPin = 2;
const int lockPin = 7;
const int doorPin = 8;
const int speakerPin = 9;
const int redLedPin = 10;
const int yellowLedPin = 11;
const int greenLedPin = 12;
const int relayPowerPin = 13;

// Analog pins
const int lightPin = 0;

enum LED {GREEN, YELLOW, RED};
enum LightState {LIGHT, DARK, NEUTRAL};

// Button variables
unsigned long startHoldTime = 0; // When the button first started being held down
unsigned long holdDelay = 2000; // Hold long it must be held down to trigger the door action

// Door variables
bool doorRunning = false;
unsigned long doorRunningStartTime = 0;
const int doorRunTime = doorRunTimeSeconds * 1000; // How long should the door motor run (in milliseconds)
const int minDoorWaitTimeHours = 10; // How long must the door keep a state before it can change states?
const unsigned long minDoorWaitTime = minDoorWaitTimeHours * 60 * 60 * 1000; // Convert the hours to milliseconds (3,600,000 per hour)
unsigned long lastDoorMotorRunTime = 0; // How long has it been since we last triggered the door motor to open or close

// Servo variables
Servo servo; // Create a servo object to control the servo

// Define the light states
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
  
  // Immediately power the relay and keep it on
  // I'm powering the relay via a pin because the 3.3v pin wasn't enough voltage to trip the relay
  pinMode(relayPowerPin, OUTPUT);
  
  lockDoor(); // Start with the door locked so a reset doesn't leave it vulnerable
  
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
    
    // Check if the button has been held to manually open/close the door
    if (digitalRead(buttonPin) == LOW) {
      // Has the button been held down long enough?
      if (millis() - startHoldTime > holdDelay) {
        if (debug) {
          Serial.println("Button pressed."); 
        }
        // Reset the hold timer to debounce the button
        startHoldTime = millis();
        // If the door is manually opened, reset the light state to whatever the light level currently is
        currentLightState = getLightState(readLightSensor());
        newStateCount = 0;
        lastDoorMotorRunTime = currentTime; // Restart the door wait countdown
        startDoorMotor();
      }
    } else {
      // Reset the hold timer
      startHoldTime = millis();
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

// Unlock the door, turn the door's power on for 30 seconds, cycle it off, lock the door
void startDoorMotor() {
  unlockDoor();
  
  if (debug) {
    Serial.println("Turning door motor on.");
  }
  lightLed(RED);
  playSuccessTone();
  doorRunning = true;
  doorRunningStartTime = millis();
  digitalWrite(relayPowerPin, HIGH); // Turn on the relay
  delay(15);
  digitalWrite(doorPin, HIGH); // Turn on the door motor
}

bool isDoorMotorDone() {
  if (doorRunning && millis() - doorRunningStartTime >= doorRunTime) {
    return true;
  }
  return false;
}

void stopDoorMotor() {
  digitalWrite(doorPin, LOW); // Turn off the door motor
  digitalWrite(relayPowerPin, LOW); // Turn off the relay
  
  if (debug) {
    Serial.println("Waited " + String((millis() - doorRunningStartTime) / 1000) + " seconds.");
    Serial.println("Turning door motor off.");
  }
  
  lockDoor();
  
  doorRunning = false;
  lightLed(GREEN);
}

void unlockDoor() {
  if (debug) {
    Serial.println("Unlocking the door.");
  }
  servo.attach(lockPin);
  delay(15);
  servo.write(unlockDoorPos);
  delay(500);
  servo.detach();
}

void lockDoor() {
  if (debug) {
    Serial.println("Locking the door.");
  }
  servo.attach(lockPin);
  delay(15);
  servo.write(lockDoorPos);
  delay(500);
  servo.detach();
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
