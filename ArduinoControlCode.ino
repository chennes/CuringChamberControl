#include <dht.h> // For reading from the DHT22 Temperature/Humidity sensor

#define UINT32_MAX uint32_t(-1)

// This is the program:
#define TEMP_TARGET 15.0       // degrees C
#define TEMP_TOLERANCE 1.5     // degrees C
#define HUMIDITY_TARGET 65.0   // RH%
#define HUMIDITY_TOLERANCE 5.0 // RH%
#define MAX_SWITCH_RATE 60000 // Milliseconds (ten minutes)

// Set up the sensor
dht DHT;
#define DHT22_PIN 2
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

#define STATE_COOLING 2
#define STATE_HEATING 3
#define STATE_DEHUMIDIFYING 4
#define STATE_HUMIDIFYING 5
#define STATE_OFF 0
#define COOLING_PIN 7
#define HUMIDIFIER_PIN 8
#define DEHUMIDIFIER_PIN 9
uint32_t _lastCheckTime;
uint32_t _lastTempStateChangeTime;
uint32_t _lastHumidityStateChangeTime;
int _tempState;
int _humidityState;

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
void getCurrentRuntime (int &days, int &hours, int &minutes, int &seconds);
void updateAverages (float temp, float humidity);


void setup() 
{
    _numWraparounds = 0;
    _runtimeInSeconds = 0;
    _lastCheckTime = 0;
    _lastTempStateChangeTime = 0;
    _lastHumidityStateChangeTime = 0;
    _tempState = STATE_OFF;
    _humidityState = STATE_OFF;
    
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
    
  pinMode(COOLING_PIN, OUTPUT);
  digitalWrite (COOLING_PIN, LOW);
    
  pinMode(DEHUMIDIFIER_PIN, OUTPUT);
  digitalWrite (DEHUMIDIFIER_PIN, LOW);
  
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
    // The time wrapped around, the math is more complex now:
    timeSinceLastTempActuation = (UINT32_MAX - _lastTempStateChangeTime) + currentTime;
    timeSinceLastHumidityActuation = (UINT32_MAX - _lastHumidityStateChangeTime) + currentTime;
    _numWraparounds++;
  } else {
    timeSinceLastTempActuation = currentTime-_lastTempStateChangeTime;
    timeSinceLastHumidityActuation = currentTime-_lastHumidityStateChangeTime;
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
    Serial.print (_tempState == STATE_COOLING ? "on":"off");
    Serial.print ("\t");
    Serial.print (_humidityState == STATE_DEHUMIDIFYING ? "on":"off");
    Serial.print ("\t");
    
    // Temperature check:
    if (_tempState == STATE_COOLING) {
      // Do we still need to be cooling?
      if (environmentData.temp < TEMP_TARGET+_averageCoolingOvershoot) {
        // Stop the cooling
        _lastTempStateChangeTime = currentTime;
        _tempState = STATE_OFF;
        digitalWrite (COOLING_PIN, LOW);
      }
    } else if (_tempState == STATE_HEATING) {
      if (environmentData.temp > TEMP_TARGET) {
        // Stop the heating
        _lastTempStateChangeTime = currentTime;
        _tempState = STATE_OFF;
      }
    } else {
      // Have we waited long enough?
      if (timeSinceLastTempActuation > MAX_SWITCH_RATE) {
        // Do we need to cool?
        if (environmentData.temp > TEMP_TARGET + TEMP_TOLERANCE) {
          // We need to cool: turn on the refrigerator's compressor and make sure the lamps are off
          digitalWrite (COOLING_PIN, HIGH);
          _tempState = STATE_COOLING;
          _lastTempStateChangeTime = currentTime;
        } else if (environmentData.temp < TEMP_TARGET - TEMP_TOLERANCE) {
          _tempState = STATE_HEATING;
          _lastTempStateChangeTime = currentTime;
        } else {
          // We are still in a good range, do nothing
        }
      }
    }
    
    
    // Humidity check:
    if (_humidityState == STATE_DEHUMIDIFYING) {
      // Do we still need to be dehumidifying?
      if (environmentData.humidity < HUMIDITY_TARGET) {
        // Stop the humidifying
        _lastHumidityStateChangeTime = currentTime;
        _humidityState = STATE_OFF;
        digitalWrite (DEHUMIDIFIER_PIN, LOW);
      }
    } else {
      // Have we waited long enough?
      if (timeSinceLastHumidityActuation > MAX_SWITCH_RATE) {
        // Do we need to dehumidify?
        if (environmentData.humidity > HUMIDITY_TARGET + HUMIDITY_TOLERANCE) {
          // We need to dehumidify: turn on the dehumidifier
          digitalWrite (DEHUMIDIFIER_PIN, HIGH);
          _humidityState = STATE_DEHUMIDIFYING;
          _lastHumidityStateChangeTime = currentTime;
        } else {
          // We are still in a good range, do nothing
        }
      }
    }
    
    updateAverages (environmentData.temp, environmentData.humidity);
    
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


