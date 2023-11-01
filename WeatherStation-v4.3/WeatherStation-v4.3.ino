/**************
 * Weather Station
 * Hardware connections:
  SHT30 -> Arduino ESP8266
    GND -> GND
    3.3 -> 3.3
    SDA -> A4
    SCL -> A5
 * 
 **************/ 
// Version 4 does not provide a DNS domain name. The MDNS code is not reliable.
// Version 4 uses the adafruit sht-30 sensor instead of the sparkfun BME280 sensor
//           I installed the Adafruit SHT31 library to use the sht30 sensor
//           To set the rtc time, run sketch named ds3231-test.
//           ds3231-test reads the time from the host and saves it in the rtc.
// Version 4.1 Implemented CSS styling for the history table.
//           The size of the history table is set using NUM_RECORDS
//           History is saved every SAVE_ON_MINUTES minutes
//           History rolls when filled, dropping the oldest record from the top
//           of the list and adding the newest record to the bottom
//           Dew point and Heat index are implemented using generally accepted formulas
//           Note that Heat index is undefined when temperature is less than 80 def F. 
// Version 4.2 is for the camper rooftop unit.
// Version 4.3 uses a five reading rolling avg approximation for wind speed, reduces the JSON
//          by only sending data, not sending tags anymore. 
#define VERSION_NUMBER "4.3"


#include <Arduino_JSON.h>

#include "Adafruit_SSD1306.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);


// Type of T & H Sensor
// #define SHT31
#define BME_280

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <math.h>

#ifdef SHT31
#include "Adafruit_SHT31.h"
#endif

#ifdef BME_280
#include "Adafruit_BME280.h"
#endif

#include "Adafruit_ADS1X15.h"

Adafruit_ADS1X15 ads;
uint16_t adc0;
uint16_t adc1;

#define UNDEFINED -99
#define APSSID "S_S_Minnow"
#define APPSK  "wtf" // Note that password is not required

const char *ssid = APSSID;
const char *password = APPSK;

// Instantiate a web server to handle http requests on default port 80
ESP8266WebServer server(80);

// Instantiate the sensor
#ifdef SHT31 
Adafruit_SHT31 th_sensor = Adafruit_SHT31();
#endif

#ifdef BME_280
Adafruit_BME280 th_sensor = Adafruit_BME280();
#endif

static char directions[][8]   = {"N", "NE", "E", "SE", "S",  "SW", "W",  "NW"};

// When an hour, minute, or second is less than 10, then
// create a String from its integer value with a leading zero.
// So a value 7 is converted to a String "07". This makes
// time values more readable.
String normalizeTimeInt(uint8_t timePart)
{
  String szTimePart = String(timePart);
  if (timePart < 10)
  {
    szTimePart = "0" + szTimePart;  
  }
  return szTimePart;
}

float mapRange (float value, float minFrom, float maxFrom, float minTo, float maxTo)
{
  float aboveMin = value - minFrom;
  if (aboveMin < 0.0)
  {
    aboveMin = 0;
  }
  if (value > maxFrom)
  {
    value = maxFrom;
  }

  float fromRange = maxFrom - minFrom;
  float toRange = maxTo - minTo;
  float adjFactor = aboveMin/fromRange;
  float mapValue = minTo + (adjFactor * toRange);
  return mapValue;
}

// Compute wind chill using formula from https://www.weather.gov/media/epz/wxcalc/windChill.pdf
String getWindChill()
{
  // Only return a number if temp is <= 50 and speed is >= 3 mph
  // Return "--" if conditions are not met
  String sWindChill = "--";
  
  String sWindSpeedMPH = getWindSpeedMPH();
  float windSpeed = sWindSpeedMPH.toFloat();

  float temperatureCelsius = th_sensor.readTemperature();
  float temperatureF = (temperatureCelsius * 9 / 5)+32;

  if (windSpeed >= 3.0 && temperatureF <= 50.0)
  {
    float fWindChill = 35.74 + \
                     (0.6215 * temperatureF) - \
                     (35.75 * pow(windSpeed,0.16)) + \
                     (0.4275 * temperatureF * pow(windSpeed,0.16));
    
    char szChill[10];
    dtostrf(fWindChill, 6, 1, szChill);
    sWindChill = String(szChill);
    sWindChill.trim();
  }
    
  return sWindChill;               
}



//-----
// APPROXIMATE rolling average
// New average = old average * (n-1)/n + new value /n
//-----
float rollingSpeedAverage (float aSpeed)
{
    static float avg = 0.0;
    avg -= avg / 5;
    avg += aSpeed / 5;

    return avg;
}
// Compute wind speed (mph) from the observed anemometer voltage output
String getWindSpeedMPH()
{

    uint16_t adcValue = ads.readADC_SingleEnded(0);

    // Weird values need to be figured out. Until then...
    // Problem seems to be when 0 volts is expected from wind vane (channel 1)
    // Channel 0 seems ok, but I put this in just in case something is seen. 
    // May
    // need a pull-down resistor on channel 0 if the wind vane is not really
    // setting ground on the input. If it's floating it would cause trouble.
    if (adcValue > 60000) 
    {
      Serial.print("Fixing adc channel 0 reset to 0");
      adcValue = 0;
    }
  
  
    // Convert the raw adc output value to a voltage based on
    // observation that no wind is 0.4 volts with an adc result of 3248.
    // So 3248 is 0.4 volts. The number 26796 used below works.
    //
    // This should have been determined by the number of adc samples 
    // (4096, which is 2**12) and the max voltage (3.3) expected
    // by the adc. But it doesn't work, probably because there is a 'gain'
    // used in the adc, which I cannot figure out.
    float voltage_val = ((float)adcValue / 26796) * 3.3;
    
    // The anemometer documentation says that output will be from 0.4 - 2.0 volts,
    // which corresponds to 0 m/sec to 32.4 m/sec. Map the voltage to a speed
    // in m/sec. Observed min voltage is 0.4, so that is used. 
    float speed = mapRange(voltage_val, 0.4, 2.0, 0.0, 32.4);
    if (speed < 0.01)
    {
        speed = 0.0;
    }
    // Speed is in m/sec...convert to mi/hr
    float speedMPH = 2.237 * speed;

    // Keep a rolling average of the last five readings and report this average
    speedMPH = rollingSpeedAverage(speedMPH);

    char szSpeed[10];
    dtostrf(speedMPH, 6, 1, szSpeed);
    String stringSpeedMPH = String(szSpeed);
    stringSpeedMPH.trim();

    
    return stringSpeedMPH;
} 

//    Convert the vane adc reading, which is one of 8 values,
//    into a compass direction.
//    Note that this uses static data directions array!
String getWindDirection ()
{
    
    uint16_t adcValue = ads.readADC_SingleEnded(1);

    // Weird values need to be figured out. Until then...
    // Problem seems to be when 0 volts is expected from wind vane. May
    // need a pull-down resistor on channel 0 if the wind vane is not really
    // setting ground on the input. If it's floating it would cause trouble.
    if (adcValue > 60000) 
    {
      Serial.print("Fixing adc channel 1 reset to 0");
      adcValue = 0;
    }

    String windDirection;
    
    // The direction voltages were experimentally determined by hand-setting
    // the wind vane to each of the 8 compass directions.
    uint16_t dirADCValues[8] = {0, 3344,   6704,  10080, 13456, 16848, 20224, 23632};
    
    int16_t minDelta = 1000;
    int16_t delta;
    uint8_t dirIndex = 0;
    
    // Select the direction by finding the closest vane voltage match
    // in the list of 8 expected voltages. Closest match is used because the
    // adc values are not repeatible with great precision.
    for (int i=0; i<8; i++)
    {
        delta = abs(adcValue - dirADCValues[i]);
        if (delta < minDelta)
        {
            minDelta = delta;
            windDirection = directions[i];
        }
    }
    
    
    return windDirection;
}


// Convert a temperature from Celsius to Fahrenheit. Return the
// result as a String value with specified decimal precision.
String convertCToF (float temperatureCelsius, uint8_t decimalPrecision)
{
  float temperature = (temperatureCelsius * 9 / 5)+32;
  char szTemperature[10];
  dtostrf(temperature, 6, decimalPrecision, szTemperature);
  return szTemperature;
}

// Use a formula for Heat Index from the National Weather Service to 
// compute the heat index. Note that this is valid only when temperature
// is 80 deg F or higher.
double computeHeatIndex(double temperatureCelsius, double humidity)
{
  double c1 = -42.379;
  double c2 = 2.04901523;
  double c3 = 10.14333127;
  double c4 = -0.22475541;
  double c5 = -6.83783e-03;
  double c6 = -5.481717e-02;
  double c7 = 1.22874e-03;
  double c8 = 8.5282e-04;
  double c9 = -1.99e-06;

  double temperature = (temperatureCelsius * 9 / 5)+32;
  double heatIndex = UNDEFINED;
  
  if (temperature >= 80.0)
  {
    heatIndex = c1 + \
                      (c2 * temperature) + \
                      (c3 * humidity) + \
                      (c4 * temperature * humidity) + \
                      (c5 * pow(temperature,2)) + \
                      (c6 * pow(humidity, 2)) + \
                      (c7 * pow(temperature,2) * humidity) + \
                      (c8 * temperature * pow(humidity, 2)) + \
                      (c9 * pow(temperature, 2) * pow(humidity, 2));
  }
  return heatIndex;
}

String getHeatIndex(double temperatureCelsius, double humidity)
{
  char szHeatIndex[10] = "--";
  double heatIndex = computeHeatIndex(temperatureCelsius, humidity);
  if (heatIndex != UNDEFINED)
  {
    dtostrf(heatIndex, 6, 1, szHeatIndex);
  }
  return szHeatIndex;
}

// Handle http requests sent to the root.  
// Go to http://192.168.4.1 in a web browser connected 
// to this access point to make a request.
void handleRoot() 
{
  String windSpeed = getWindSpeedMPH();
  String windDirection = getWindDirection(); 
  String windChill = getWindChill();
  float temperatureCelsius = th_sensor.readTemperature();
  
  String szTemperature = convertCToF(temperatureCelsius, 1);
  Serial.print("temp = ");
  Serial.println(szTemperature);
  
  float humidity = th_sensor.readHumidity();
  char szHumidity[10];
  dtostrf(humidity, 6, 1, szHumidity);
  float dewPointCelsius = calculateDewPoint(temperatureCelsius, humidity);
  String szDewPoint = convertCToF(dewPointCelsius, 1);
  String szHeatIndex = getHeatIndex(temperatureCelsius, humidity);

  // Use global values for windSpeed and windDirection. These are set from
  // the main loop.
  
  String response = "<!DOCTYPE html>";
  response += "<html>";
  response += "<head>";
  response += "<style>";
  response += "body {font-size: 140%;}";
  response += "table, th, td {";
  response += "border: 1px solid black;";
  response += "}";

  response += "td {";
  response += "text-align: center;";
  response += "}";
  
  response += "table {";
  response += "border-collapse: collapse;";
  response += "width: 100%;";
  response += "}";
  response += "</style>";
  response += "<meta name='viewport' content='width=device-width, initial-scale=1.0'/>";
  response += "<title>S.S. Minnow Weather Station</title>";
  response += "</head>";
  
  response += "<body>";
  response += "<h3>S.S. Minnow Weather Station</h3>";
  response += "<ul><li>Wind speed: ";
  response += windSpeed;
  response += " mph from the ";
  response += windDirection;
  response += "</li><li>Wind chill: ";
  response += windChill;
  response += "</li><li>Temperature: ";
  response += szTemperature;
  response += "</li><li>Humidity: ";
  response += szHumidity;
  response += "</li><li>Dew point: ";
  response += szDewPoint;
  response += "</li><li>Heat index: ";
  response += szHeatIndex;
  response += "</li></ul>";
  response += "</body></html>";
  server.send(200, "text/html", response);
}

void handleJSON()
{
  String windSpeed = getWindSpeedMPH();
  String windDirection = getWindDirection(); 
  String windChill = getWindChill();

  float temperatureCelsius = th_sensor.readTemperature();
  String sTemperature = convertCToF(temperatureCelsius, 1);
  sTemperature.trim();
  
  float fHumidity = th_sensor.readHumidity();
  char humidity[10];
  dtostrf(fHumidity, 6, 1, humidity);
  String sHumidity = String(humidity);
  sHumidity.trim();
  
  float dewPointCelsius = calculateDewPoint(temperatureCelsius, fHumidity);
  String sDewPoint = convertCToF(dewPointCelsius, 1);
  sDewPoint.trim();
  
  String sHeatIndex = getHeatIndex(temperatureCelsius, fHumidity);
  sHeatIndex.trim();

  JSONVar weatherInfo;
  JSONVar weatherData;

  /*
  JSONVar dataTags;
  
  dataTags[0]="windSpeed";
  dataTags[1]="windDirection";
  dataTags[2]="windChill";
  dataTags[3]="temperature";
  dataTags[4]="humidity";
  dataTags[5]="dewpoint";
  dataTags[6]="heatindex";
  */
  
  weatherData[0]=windSpeed;
  weatherData[1]=windDirection;
  weatherData[2]=windChill;
  weatherData[3]=sTemperature;
  weatherData[4]=sHumidity;
  weatherData[5]=sDewPoint;
  weatherData[6]=sHeatIndex;

  weatherInfo[0]=weatherData;
  // weatherInfo[1]=dataTags;
  
  String jsonString = JSON.stringify(weatherInfo);
 
  server.send(200, "application/json", jsonString);
}

float calculateDewPoint(float temperatureCelsius, float relativeHumidity)
{
  float a = 17.27;
  float b = 237.7;
  
  float alpha = ((a * temperatureCelsius)/(b+temperatureCelsius)) + log(relativeHumidity/100.0);

  float dewPoint = (b * alpha) / (a - alpha);

  return dewPoint;
}

void setup() 
{
  delay(1000);
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("Version number: "+VERSION_NUMBER);
  Serial.print("Configuring access point...");
  
  // Do not specify the password parameter if you want the AP to be open. 
  // WiFi.softAP(ssid, password);

  // Open Access Point with ssid set to S_S_Minnow
  WiFi.softAP(ssid);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  
  server.on("/", handleRoot);
  server.on("/json", handleJSON);
  server.begin();
  Serial.println("HTTP server started");

  // some sensors are 0x44 or 0x45
  // SparkFun sensors are 0x76 or 0x77
  int i2c_sensor_addr = 0;
  #ifdef SHT31
  i2c_sensor_addr = 0x44;
  #endif

  #ifdef BME_280
  i2c_sensor_addr = 0x77;
  #endif
  if (! th_sensor.begin(i2c_sensor_addr)) 
  {   
    Serial.println("Couldn't start the T & H Sensor");
    while (1) delay(1);
  }

  // Gain set to GAIN_ONE seems to work best
  ads.setGain(GAIN_ONE);
  ads.begin();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  // display.display();
  // delay(2000);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  
}

uint16_t loopCounter = 0;

void loop() 
{
  server.handleClient();

  delay(5000);
  
  String windSpeed = getWindSpeedMPH();
  String windDirection = getWindDirection(); 
  String windChill = getWindChill();
  Serial.print(windSpeed);
  Serial.print(";");
  Serial.print(windDirection);
  Serial.print(";");
  Serial.println(windChill);

  // Show values on small OLED screen mounted with the outside equipment
  String sLoopCounter = String(loopCounter);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("S. S. Minnow: "+sLoopCounter);
  display.setCursor(0,10);
  display.print("Wind: "+windSpeed+", "+windDirection);
  display.setCursor(0,20);

  // @TODO Refactor: Wrap all the temp and humidity readings into functions and call from
  //       each place that uses them
  float temperatureCelsius = th_sensor.readTemperature();
  String sTemperature = convertCToF(temperatureCelsius, 1);
  sTemperature.trim();
  
  float fHumidity = th_sensor.readHumidity();
  char humidity[10];
  dtostrf(fHumidity, 6, 1, humidity);
  String sHumidity = String(humidity);
  sHumidity.trim();

  display.print("T & H: "+sTemperature+", "+sHumidity);
  display.display();
  loopCounter += 1;
}
