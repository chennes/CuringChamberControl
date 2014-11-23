#include <dht.h> // For reading from the DHT22 Temperature/Humidity sensor

// Expected Arduino Uno Pinout (All OUTPUT except pin 8):
// 0  - None (Rx)
// 1  - None (Tx)
// 2  - LCD RS
// 3  - LCD Enable
// 4  - LCD d4
// 5  - LCD d5
// 6  - LCD d6
// 7  - LCD d7
// 8  - DHT22 (input pin)
// 9  - Refrigerator
// 10 - Heater
// 11 - Humidifier
// 12 - Dehumidifier
// 13 - None

#define UINT32_MAX uint32_t(-1)

// This is the program:
float TEMP_TARGET = 15.0;       // degrees C
float TEMP_TOLERANCE = 1.5;     // degrees C
float HUMIDITY_TARGET = 65.0;   // RH%
float HUMIDITY_TOLERANCE = 5.0; // RH%
uint32_t MAX_SWITCH_RATE = 300; // Seconds (five minutes)

// Set up the sensor
dht DHT;
#define DHT22_PIN 8
struct {
  uint32_t total;
  uint32_t ok;
  uint32_t crc_error;
  uint32_t time_out;
  uint32_t unknown;
} stat = { 0,0,0,0,0 };

struct DH22Reading {
  float temp;
  float humidity;
  int check;
};

enum State {
  NOT_CONTROLLING,
  CONTROLLING_UP,
  CONTROLLING_DOWN
};

struct ControlledValue {
  float current;
  float target;
  float tolerance;
  int pinToIncrease;
  int pinToDecrease;
  uint32_t lastStateChangeTime; // In **seconds**, not milliseconds!
  State state;
};

ControlledValue temperature = {0, TEMP_TARGET, TEMP_TOLERANCE, 10, 9, 0, NOT_CONTROLLING };
ControlledValue humidity = {0, HUMIDITY_TARGET, HUMIDITY_TOLERANCE, 11, 12, 0, NOT_CONTROLLING };

uint32_t _lastCheckTime;

unsigned long _numWraparounds; // How many times has the clock wrapped
unsigned long _runtimeInSeconds; // 4 bytes, so a max of 4,294,967,295 (something like 136 years)


// Rolling Averages, designed to keep storage requirements reasonable
unsigned long _lastAverageUpdateSeconds;
unsigned long _hourOfLastAverageUpdate;
unsigned long _dayOfLastAverageUpdate;
float _tempsLastHour[60];
float _tempsLastDay[24];
float _tempsLastMonth[30];
float _humiditiesLastHour[60];
float _humiditiesLastDay[24];
float _humiditiesLastMonth[30];
float _averageTempLastHour;
float _averageTempLastDay;
float _averageTempLastMonth;
float _averageHumidityLastHour;
float _averageHumidityLastDay;
float _averageHumidityLastMonth;
float _averageCoolingOvershoot;


// Forward declarations
DH22Reading readDH22 ();
void updateState (ControlledValue &variable);
void getCurrentRuntime (int &days, int &hours, int &minutes, int &seconds);
void updateAverages (float temp, float humidity);
void printTime ();


void setup() 
{
    _numWraparounds = 0;
    _runtimeInSeconds = 0;
    _lastCheckTime = 0;
    
    for (int i = 0; i < 60; ++i) {
      _tempsLastHour[i] = 0;
      _humiditiesLastHour[i] = 0;
    }
    for (int i = 0; i < 24; ++i) {
      _tempsLastDay[i] = 0;
      _humiditiesLastDay[i] = 0;
    }
    for (int i = 0; i < 30; ++i) {
      _tempsLastMonth[i] = 0;
      _humiditiesLastMonth[i] = 0;
    }
    
  _lastAverageUpdateSeconds = 0;
  _hourOfLastAverageUpdate = 10000; // Make sure it hits the first time by initializing to nonsense
  _dayOfLastAverageUpdate = 10000; // Make sure it hits the first time by initializing to nonsense
  
  
  _averageTempLastHour = 0;
  _averageTempLastDay = 0;
  _averageTempLastMonth = 0;
  _averageHumidityLastHour = 0;
  _averageHumidityLastDay = 0;
  _averageHumidityLastMonth = 0;
  _averageCoolingOvershoot = 0.0;
    
  pinMode(temperature.pinToIncrease, OUTPUT);
  pinMode(temperature.pinToDecrease, OUTPUT);
  pinMode(humidity.pinToIncrease, OUTPUT);
  pinMode(humidity.pinToDecrease, OUTPUT);
  digitalWrite (temperature.pinToIncrease, LOW);
  digitalWrite (temperature.pinToDecrease, LOW);
  digitalWrite (humidity.pinToIncrease, LOW);
  digitalWrite (humidity.pinToDecrease, LOW);
  
  Serial.begin(115200);
  Serial.println ("\n\tDay\tHour\tMin\tSec\tT\tRH\tCool\tDehum.\tRunning Averages");
}



void loop() 
{
  // Get the time since the last actuation. Be sure to handle time wraparound!!
  uint32_t timeSinceLastTempActuation;
  uint32_t timeSinceLastHumidityActuation;
  uint32_t currentTime = millis();
  if (currentTime < _lastCheckTime) {
    // Happens once every 50 days or so
    _numWraparounds++;
  } 
  _lastCheckTime = currentTime;
  _runtimeInSeconds = _numWraparounds * (UINT32_MAX/1000) + (currentTime/1000);
  const uint32_t pollRate = 15000; // Defaults to every fifteen seconds in the normal case
  
  // Read the sensor
  DH22Reading environmentData = readDH22();
  
  if (environmentData.check == DHTLIB_OK) {
    printTime();
    Serial.print (environmentData.temp,1);
    Serial.print ("\t");
    Serial.print (environmentData.humidity,1);
    Serial.print ("\t");
    Serial.print (temperature.state == CONTROLLING_DOWN ? "on":"off");
    Serial.print ("\t");
    Serial.print (humidity.state == CONTROLLING_DOWN ? "on":"off");
    Serial.print ("\t");

    temperature.current = environmentData.temp;
    humidity.current = environmentData.humidity;
    updateState (temperature);
    updateState (humidity);
    updateAverages (temperature.current, humidity.current);
    
    // For now, print the averages every time (when they are valid):
    if (_averageTempLastHour > 0) {
      Serial.print ("Hourly: ");
      Serial.print (_averageTempLastHour);
      Serial.write (176); // The degree symbol
      Serial.print ("C ");
      Serial.print (_averageHumidityLastHour);
        Serial.print ("% ");
      if (_averageTempLastDay > 0) {
        Serial.print (", Daily: ");
        Serial.print (_averageTempLastDay);
        Serial.write (176); // The degree symbol
        Serial.print ("C ");
        Serial.print (_averageHumidityLastDay);
        Serial.print ("% ");
        if (_averageTempLastMonth > 0) {
          Serial.print (", Monthly: ");
          Serial.print (_averageTempLastMonth);
          Serial.write (176); // The degree symbol
          Serial.print ("C ");
          Serial.print (_averageHumidityLastMonth);
        Serial.print ("% ");
        }
      }
    }
    Serial.println ();
  } else {
    Serial.println ("ERROR: Cannot read from sensor!!");
  }

  delay (pollRate);
}


DH22Reading readDH22 ()
{
  // READ DATA
  int chk = DHT.read22(DHT22_PIN);
  DH22Reading returnValue;
  returnValue.check = chk;
  returnValue.temp = DHT.temperature;
  returnValue.humidity = DHT.humidity;
  return returnValue;
}


// An utterly naive bang-bang controller with a timeout to prevent too-frequent activations 
void updateState (ControlledValue &variable)
{
  switch (variable.state) {
    case NOT_CONTROLLING:
      if (_runtimeInSeconds > variable.lastStateChangeTime+MAX_SWITCH_RATE) {
        if (variable.current > variable.target + variable.tolerance) {
          variable.lastStateChangeTime = _runtimeInSeconds;
          variable.state = CONTROLLING_DOWN;
          digitalWrite (variable.pinToDecrease, HIGH);
        } else if (variable.current < variable.target - variable.tolerance) {
          variable.lastStateChangeTime = _runtimeInSeconds;
          variable.state = CONTROLLING_UP;
          digitalWrite (variable.pinToIncrease, HIGH);
        }
      }
      break;
    case CONTROLLING_UP:
      if (variable.current > variable.target) {
        variable.lastStateChangeTime = _runtimeInSeconds;
        variable.state = NOT_CONTROLLING;
        digitalWrite (variable.pinToIncrease, LOW);
      }
      break;
    case CONTROLLING_DOWN:
      if (variable.current < variable.target) {
        variable.lastStateChangeTime = _runtimeInSeconds;
        variable.state = NOT_CONTROLLING;
        digitalWrite (variable.pinToDecrease, LOW);
      }
      break;
  }
}


void getCurrentRuntime (unsigned long &days, unsigned long &hours, unsigned long &minutes, unsigned long &seconds)
{
  const unsigned long secondsPerMinute = 60;
  const unsigned long secondsPerHour = 3600;
  const unsigned long secondsPerDay = 86400;
  
  unsigned long computationSeconds = _runtimeInSeconds;
  days = computationSeconds / secondsPerDay;
  computationSeconds = computationSeconds % secondsPerDay;
  hours = computationSeconds / secondsPerHour;
  computationSeconds = computationSeconds % secondsPerHour;
  minutes = computationSeconds / secondsPerMinute;
  computationSeconds = computationSeconds % secondsPerMinute;
  seconds = computationSeconds;
}


void printTime ()
{
  unsigned long days, hours, minutes, seconds;
  getCurrentRuntime (days, hours, minutes, seconds);
  Serial.print (_runtimeInSeconds);
  Serial.print ("\t");
  Serial.print (days);
  Serial.print ("\t");
  Serial.print (hours);
  Serial.print ("\t");
  Serial.print (minutes);
  Serial.print ("\t");
  Serial.print (seconds);
  Serial.print ("\t");
}


void updateAverages (float temp, float humidity)
{
  unsigned long days, hours, minutes, seconds;
  getCurrentRuntime (days, hours, minutes, seconds);
  if (_runtimeInSeconds - _lastAverageUpdateSeconds  >= 60) {
    
    // We just assume it's been about a minute (our usual polling rate)
    _lastAverageUpdateSeconds = _runtimeInSeconds; 
    
    // We are just going to do the easiest (and slowest) method here, shifting ALL the data, instead
    // of keeping a pointer to the oldest.
    float humidityIntegrator = 0.0;
    float tempIntegrator = 0.0;
    unsigned int count = 1;
    for (int i = 0; i < 59 /*The last slot gets the current data*/; ++i) {
      if (_tempsLastHour[i+1] > 0.0) { // Has it been set yet?
        count++;
        _tempsLastHour[i] = _tempsLastHour[i+1];
        tempIntegrator += _tempsLastHour[i];
        _humiditiesLastHour[i] = _humiditiesLastHour[i+1];
        humidityIntegrator += _humiditiesLastHour[i];
      }
    }
    _tempsLastHour[59] = temp;
    tempIntegrator += _tempsLastHour[59];
    _humiditiesLastHour[59] = humidity;
    humidityIntegrator += _humiditiesLastHour[59];
    
    _averageTempLastHour = tempIntegrator / count;
    _averageHumidityLastHour = humidityIntegrator / count;
    
    if (minutes == 0 && _hourOfLastAverageUpdate != hours) {
      _hourOfLastAverageUpdate = hours; // Make sure it only happens once per hour
    
      tempIntegrator = 0;
      humidityIntegrator = 0;  
      // Do the daily average the first minute of every hour:
      count = 1;
      for (int i = 0; i < 23; ++i) {
        if (_tempsLastDay[i+1] > 0.0) { // Has it been set yet?
          count++;
          _tempsLastDay[i] = _tempsLastDay[i+1];
          tempIntegrator += _tempsLastDay[i];
          _humiditiesLastDay[i] = _humiditiesLastDay[i+1];  
          humidityIntegrator += _humiditiesLastDay[i];
        }
      }
      _tempsLastDay[23] = _averageTempLastHour;
      tempIntegrator += _tempsLastDay[23];
      _humiditiesLastDay[23] = _averageHumidityLastHour;
      humidityIntegrator += _humiditiesLastDay[23];
        
      _averageTempLastDay = tempIntegrator / count;
      _averageHumidityLastDay = humidityIntegrator / count;
        
      if (hours == 0 && _dayOfLastAverageUpdate != days) {
        _dayOfLastAverageUpdate = days; // Make sure it only happens once per day
        tempIntegrator = 0;
        humidityIntegrator = 0;
        count = 1;
        for (int i = 0; i < 29; ++i) {
          if (_tempsLastMonth[i+1] > 0.0) {
            count++;
            _tempsLastMonth[i] = _tempsLastMonth[i+1];
            tempIntegrator += _tempsLastMonth[i];
            _humiditiesLastMonth[i] = _humiditiesLastMonth[i+1];  
            humidityIntegrator += _humiditiesLastMonth[i];
          }
        }
        _tempsLastMonth[29] = _averageTempLastDay;
        tempIntegrator += _tempsLastMonth[29];
        _humiditiesLastMonth[29] = _averageHumidityLastDay;
        humidityIntegrator += _humiditiesLastMonth[29];
            
        _averageTempLastMonth = tempIntegrator / count;
        _averageHumidityLastMonth = humidityIntegrator / count;
        
        // Update the overshoot based on the average temp for the last day
        float tOvershoot = TEMP_TARGET - _averageTempLastDay;
        // Adjust the overshoot correction value:
        _averageCoolingOvershoot += tOvershoot;
        Serial.print (" Overshoot updated to ");
        Serial.print (_averageCoolingOvershoot);
        Serial.write (176); // The degree symbol
        Serial.print ("C. ");
      }
    }
  }
}


