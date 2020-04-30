#include <Debounce.h>


#include "lib1.h"

#include <Ubidots.h>

#include "Particle.h"
#include "AssetTrackerRK.h"
#include <Adafruit_MPRLS.h>
#include "TCA9548A-RK.h"
#include <Adafruit-MotorShield-V2.h>
 
#define RESET_PIN  -1  // set to any GPIO pin # to hard-reset on begin()
#define EOC_PIN    -1  // set to any GPIO pin to read end-of-conversion by pin
  
#ifndef UBIDOTS_TOKEN
#define UBIDOTS_TOKEN "****" // Put here your Ubidots TOKEN
#endif 

Ubidots ubidots(UBIDOTS_TOKEN, UBI_UDP);
  
  
SYSTEM_THREAD(ENABLED);

void displayInfo(); // forward declaration

const unsigned long PUBLISH_PERIOD = 120000;
const unsigned long SERIAL_PERIOD = 5000;

// GPS variables and parameters
AssetTracker t;
unsigned long lastSerial = 0;
unsigned long lastPublish = 0;
unsigned long startFix = 0;
bool gettingFix = false;
int seatAlert = 0;
boolean inSeat = true;

// reset seat alert button, currently assigned to D2
long currentResetDebounce, previousResetDebounce;
long debounceInterval = 75;
int currentResetButton, previousResetButton;
int resetButtonState = HIGH;

char lastLocation[128]; // used for out of seat check alert

double lastLat = 0;
double lastLon = 0;

// Seat check variables and parameters
int leftSeatSensor, rightSeatSensor;
long currentSeatCheck, previousSeatCheck;
const long seatCheckInterval = 1500;


// sip/puff control variables and parameters
Adafruit_MPRLS mpr = Adafruit_MPRLS(RESET_PIN, EOC_PIN);
TCA9548A mux0(Wire, 0);
long currentSelect, previousSelect;
const long selectInterval = 50;

int motorIndex;
const int maxIndex = 5;

int lights[] = {7, 4, 6, 5, 3, 8};

// Motor variables and parameters
Adafruit_MotorShield AFMS0 = Adafruit_MotorShield(); 
Adafruit_MotorShield AFMS1 = Adafruit_MotorShield(0x61); 

Adafruit_DCMotor *baseMotor     = AFMS0.getMotor(1);
Adafruit_DCMotor *shoulderMotor = AFMS0.getMotor(2);
Adafruit_DCMotor *elbowMotor    = AFMS0.getMotor(3);
Adafruit_DCMotor *wristMotor    = AFMS0.getMotor(4);
Adafruit_DCMotor *gripMotor     = AFMS1.getMotor(2);
Adafruit_DCMotor *lightMotor    = AFMS1.getMotor(1);


Debounce debouncer = Debounce();

long currentUbidots, previousUbidots;
long ubidotsUpdateInterval = 2500;

bool updateUbidots()
{
   
  char* str_lat = (char *) malloc(sizeof(char)* 10);
  char* str_lng = (char *) malloc(sizeof(char)* 10);
  sprintf(str_lat, "%f", lastLat);
  sprintf(str_lng, "%f", lastLon);
  char* context = (char *) malloc(sizeof(char) * 30);
    
  ubidots.addContext("lat",str_lat);
  ubidots.addContext("lng",str_lng);
  ubidots.getContext(context); 
  ubidots.add("position", 1.0, context); 
  
  ubidots.add("Left_Seat_Sensor", leftSeatSensor);  // Change for your variable name
  ubidots.add("Right_Seat_Sensor", rightSeatSensor);

  bool bufferSent = false;
  bufferSent = ubidots.send(); 
  
  free(str_lat);
  free(str_lng);
  free(context);
    
}

void setup()
{
	Serial.begin();
	// Turn on GPS module
	
	Particle.variable("seatAlert", seatAlert);
    Particle.variable("latitude", lastLat);
    Particle.variable("longitude", lastLon);
	
	
	t.gpsOn();

	// Run in threaded mode - this eliminates the need to read Serial1 from loop or updateGPS() and dramatically
	// lowers the risk of lost or corrupted GPS data caused by blocking loop for too long and overflowing the
	// 64-byte serial buffer.
	t.startThreadedMode();

    startFix = millis();
    gettingFix = true;

    // If using an external antenna, uncomment this line:
    // t.antennaExternal();
    
    mux0.begin();
	mux0.setChannel(1);
	
	Serial.println("MPRLS Simple Test");
    if (! mpr.begin()) 
    {
        Serial.println("Failed to communicate with MPRLS sensor, check wiring?");
    }
  
    Serial.println("Found MPRLS sensor");
    
    // motor indicator lights
    pinMode(D8, OUTPUT);
    pinMode(D7, OUTPUT);
    pinMode(D6, OUTPUT);
    pinMode(D5, OUTPUT);
    pinMode(D4, OUTPUT);
    pinMode(D3, OUTPUT);
    
    // out of seat reset button
    //pinMode(D2, INPUT);
    debouncer.attach(D2, INPUT_PULLUP);
    debouncer.interval(35);
    
    
    
    
    // seat pressure/force resistor sensors
    pinMode(A0, INPUT);
    pinMode(A1, INPUT);
    currentSeatCheck = millis();
    previousSeatCheck = currentSeatCheck;
    
    
    AFMS0.begin();
    AFMS1.begin();
    baseMotor->setSpeed(250);
    shoulderMotor->setSpeed(250);
    elbowMotor->setSpeed(250);
    wristMotor->setSpeed(250);
    gripMotor->setSpeed(250);
    lightMotor->setSpeed(250);
}

void loop()
{
	// In threaded mode, you must not call updateGPS() from loop - it's handled automatically
	// from a separate thread if you call t.startThreadedMode()
    // Serial.println("check");
    // Out of seat alert check:
    currentSeatCheck = millis();
    if ((currentSeatCheck - previousSeatCheck) > seatCheckInterval)
    {
            previousSeatCheck = currentSeatCheck;
            leftSeatSensor = analogRead(A0);
            rightSeatSensor = analogRead(A1);
            // Serial.print("Checking Seat: ");
            // Serial.print("LS: ");Serial.print(leftSeatSensor);
            // Serial.print(" RS: ");Serial.print(rightSeatSensor);
            // Serial.print(" SeatCheck(inSeat, seatALert) ");Serial.print("(");
            // Serial.print(inSeat);Serial.print(",");Serial.print(seatAlert);
            // Serial.println(")");
            
            if ( (leftSeatSensor > 1200) || (rightSeatSensor > 1200))
            {
        	    
        	    if (inSeat) 
        	    {
                    inSeat = false;
                    seatAlert = 1;
                    
                    String smsLat = String(lastLat);
                    String smsLon = String(lastLon);
                    String emerGPS = String("OUT OF SEAT @ LAT: " + smsLat + " and LON: " + smsLon  );
                    
        	        // Particle.publish("ALERT", "out", PRIVATE);
        	        // Particle.publish("twilio_sms", "emergency out of seat", PRIVATE);
        	        // Particle.publish("twilio_sms", emerGPS, PRIVATE);
        	       // Serial.print("OUT OF SEAT _ALERT_: ");Serial.println(seatAlert);
        	    }
            }
    }
    
    debouncer.update();

    if ((debouncer.read()==HIGH) && (inSeat == false))
    {
        
        inSeat = true;
    } else {
        // do nothing.
    }
    
    // Sip-Puff controller check
    currentSelect = millis();
    if ((currentSelect - previousSelect)>selectInterval)
    {
        previousSelect = currentSelect;
        
        mux0.setChannel(0);
        float pressure_hPa0 = mpr.readPressure();
        
        Serial.print("Pressure0 (hPa): "); Serial.println(pressure_hPa0);
        
        if ( pressure_hPa0 > 1050 ) 
        {
            motorIndex++;
            if (motorIndex > maxIndex) { motorIndex = 0;} // roll over
            // Serial.print("TO NEXT FUNCTION: "); Serial.println(motorIndex);
        } else if (pressure_hPa0 < 900) {
            motorIndex--;
            if (motorIndex < 0) { motorIndex = maxIndex; } // roll over
            // Serial.print("TO PREVIOUS FUNCTION: ");Serial.println(motorIndex);
        } else {
           //  Serial.print("NO SELECTION CONDITION: ");Serial.println("functionIndex");
        }
        
       // Serial.print("MOTOR INDEX: ");
        // Serial.print(motorIndex);
        
        // Serial.print(" DIGITAL INDEX: ");
        // Serial.println(lights[motorIndex]);
        
        for (int i=0; i<6; i++)
        {
            if (i == motorIndex)
            {
                digitalWrite(lights[i], true);
            } else {
                digitalWrite(lights[i], false);
            }
        }
        
        mux0.setChannel(1);
        float pressure_hPa1 = mpr.readPressure();
        int direction = 0;
         Serial.print("Pressure1 (hPa): "); Serial.println(pressure_hPa1);
        if (pressure_hPa1 > 1050) { direction = 1; }
        else if ( pressure_hPa1 < 900 ) { direction = -1; }
        else { direction = 0; }
        
        // Serial.print("DIRECTION: ");Serial.println(direction);
 
        switch (motorIndex)
        {
            case 0:
                if (direction > 0) { baseMotor->run(FORWARD); }
                else if (direction < 0) { baseMotor->run(BACKWARD); }
                else { baseMotor->run(RELEASE); }
                break;
            case 1:
                if (direction > 0) {shoulderMotor->run(FORWARD);}
                else if (direction < 0) {shoulderMotor->run(BACKWARD);}
                else { shoulderMotor->run(RELEASE); }
                break;
            case 2:
                if (direction > 0) {elbowMotor->run(FORWARD);}
                else if (direction < 0) {elbowMotor->run(BACKWARD);}
                else { elbowMotor->run(RELEASE); }
                break;
            case 3:
                if (direction > 0) {wristMotor->run(FORWARD);}
                else if (direction < 0) {wristMotor->run(BACKWARD);}
                else { wristMotor->run(RELEASE); }
                break;
            case 4:
                if (direction > 0) { gripMotor->run(FORWARD); }
                else if (direction < 0) { gripMotor->run(BACKWARD); }
                else { gripMotor->run(RELEASE); }
                break;
            case 5:
                if (direction > 0) { lightMotor->run(FORWARD);}
                else if (direction < 0) { lightMotor->run(BACKWARD);}
                else { lightMotor->run(RELEASE); }
                break;        
        }
    }    
    
    
    currentUbidots = millis();
    if ((currentUbidots-previousUbidots)>ubidotsUpdateInterval)
    {
        // Particle.publish
        previousUbidots = currentUbidots;
        if ( updateUbidots()) {
            
            // Serial.println("UBiDOTS UPDATED :)");
        } else {
            // Serial.println("UBIDOTS NOT UPDATE :(");
        }
        
    }
    
    
    
	displayInfo();
}

void displayInfo()
{
	if (millis() - lastSerial >= SERIAL_PERIOD) {
		lastSerial = millis();

		char buf[128];
		if (t.gpsFix()) {
			snprintf(buf, sizeof(buf), "location:%f,%f altitude:%f satellites:%d hdop:%d", t.readLatDeg(), t.readLonDeg(), t.getAltitude(), t.getSatellites(), t.getTinyGPSPlus()->getHDOP().value());
			// snprintf(lastLocation, sizeof(lastLocation), "location:%f,%f altitude:%f satellites:%d hdop:%d", t.readLatDeg(), t.readLonDeg(), t.getAltitude(), t.getSatellites(), t.getTinyGPSPlus()->getHDOP().value());
			snprintf(lastLocation, sizeof(lastLocation), "{\"position\": {\"value\":1, \"context\":{\"lat\": \"%f\", \"lng\": \"%f\"}}}", t.readLatDeg(), t.readLonDeg());
			lastLat = t.readLatDeg();
			lastLon = t.readLonDeg();
			
			if (gettingFix) {
				gettingFix = false;
				unsigned long elapsed = millis() - startFix;
				Serial.printlnf("%lu milliseconds to get GPS fix", elapsed);
			}
		}
		else {
			snprintf(buf, sizeof(buf), "no location satellites:%d", t.getSatellites());
			if (!gettingFix) {
				gettingFix = true;
				startFix = millis();
			}
		}
		Serial.println(buf);

		if (Particle.connected()) {
			if (millis() - lastPublish >= PUBLISH_PERIOD) {
				lastPublish = millis();
				Particle.publish("gps", buf, PRIVATE);
			}
		}
	}

}
