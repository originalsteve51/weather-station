#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// Version 1: Display version number in Serial monitor 
//       1.1: Add history display
//       1.2: Add large number displays for in and out temperature
//       1.3: Add option switches
//       1.4: Refactor code, no added functions

#define VERSION_NUMBER 1.4

#include <Arduino_JSON.h>

#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti WiFiMulti;

#include "RTClib.h"
#include <Wire.h>


#include <Adafruit_GFX.h>     // Graphics library
#include <Adafruit_ST7735.h>  // ST7735 TFT library
#include "Adafruit_SHT31.h"   // Temp/Humidity sensor library
#include <Adafruit_MPL3115A2.h> // Barometeric pressure sensor library
#include <Adafruit_MCP23X17.h> // Port extender to support option switches (not enough gpio pins on chip)
 
// Colors for this display are specified as 5 bits Blue, 6 bits green, 5 bits red
#define MY_BLUE 0xFA00
#define MY_RED  0x001F
#define MY_GREEN 0x07E0
#define MY_BLACK 0x0000
#define MY_WHITE 0xFFFF
#define MY_YELLOW 0x07FF

// Screen line addressing (pixel locations in Y direction, 0 at top)
#define FIRST_LINE 0
#define TOP_DATA_LINE 40
#define  LINE_SPACE 10

// Screen addressing-line begin Xcoordinate
#define LINE_BEGIN_X 0

// ST7735 TFT module connections
#define TFT_RST   D4 //  TFT RST pin is connected to NodeMCU pin D4 (GPIO2)
#define TFT_CS    D3 //  TFT CS  pin is connected to NodeMCU pin D3 (GPIO0)
#define TFT_DC    D1 // Changed from D2 : TFT DC  pin is connected to NodeMCU pin D2 (GPIO4)

// ST7735 TFT uses SPI pins as follows
// SCK (CLK) ---> NodeMCU pin D5 (GPIO14)
// MOSI(DIN) ---> NodeMCU pin D7 (GPIO13)

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST); // TFT display
 
// set WiFi network SSID and password
const char *ssid     = "S_S_Minnow";
const char *password = "";
// The url/path where data is requested
const char* jsonURL = "http://192.168.4.1/json";
// String jsonData;

// unsigned long previousMillis = 0;

// interval sets how many milliseconds between updates to screen
const long interval = 30000;

unsigned long requestCount = 0;
unsigned long successCount = 0;


// Real time clock
RTC_DS3231 rtc;  

// T & H sensor
#define TEMPERATURE 0
#define HUMIDITY 1
Adafruit_SHT31 sht31 = Adafruit_SHT31();  

// Barometric pressure sensor

Adafruit_MPL3115A2 baro; 
#define  SW_0_PIN 7
#define  SW_1_PIN 6

Adafruit_MCP23X17 mcp; // Options switches via port extender

float minTemp = 999.0;
String minTempTime = "";
float maxTemp = -999.0;
String maxTempTime = "";

float minTempIn = 999.0;
String minTempTimeIn = "";
float maxTempIn = -999.0;
String maxTempTimeIn = "";

float maxWindSpeed = 0.0;
float minWindChill = 999.0;
int windDirCount[8] = {0,0,0,0,0,0,0,0};
String windDirectionPcts[8] = {"0", "0", "0", "0", "0", "0", "0", "0"};

int _normalTextClr = MY_WHITE;
int _outsideTextClr = MY_GREEN;
int _insideTextClr = MY_GREEN; 
bool _isNightMode = false;
  
// For display in large font
String _currentOutTemp = "";
String _currentInTemp = "";
String _windDirn = "";

// Every 30 minutes record data for 12 hour history
String tempHist[24];
String humHist[24];

// Forward declarations
float getTempHumidityFloat(int choice);
String getTempHumidity(int choice);
String getBarometricPressure();
void displayDataLine(String dataLabel, String sValue1, String sValue2, int _normalTextClr, int _outsideTextClr, int _insideTextClr);
String tellTime(bool minutesOnly);
void calculateDirectionPct();
void displayHeader(bool);

// Outside data values accessed using these index values
#define WIND_SPEED 0
#define WIND_DIRECTION 1
#define WIND_CHILL 2
#define TEMP 3
#define HUM 4
#define DEWPOINT 5
#define HEAT_INDEX 6

// Ordinal directions for array indexing
#define NORTH 0
#define NORTHEAST 1
#define EAST 2
#define SOUTHEAST 3
#define SOUTH 4
#define SOUTHWEST 5
#define WEST 6
#define NORTHWEST 7
void getCurrentData()
{
  
}

// Global current data set by getCurrentData() and used by many
String sInsideTemperature;
float fInsideTemperature;
String sInsideHumidity;
String sBaroPressure;

void getCurrentData(String jsonData)
{
  sInsideTemperature = getTempHumidity(TEMPERATURE);
  fInsideTemperature = getTempHumidityFloat(TEMPERATURE);
  sInsideHumidity = getTempHumidity(HUMIDITY);
  sBaroPressure = getBarometricPressure();
  
  JSONVar myArray = JSON.parse(jsonData);
  
  float newTemp = String(myArray[0][TEMP]).toFloat();
        
  // Make values to display in large numbers with no decimal places
  _currentOutTemp = String(newTemp,0);
  _currentInTemp = String(sInsideTemperature.toFloat(),0);

  _windDirn = String(myArray[0][WIND_DIRECTION]);
}

void showCurrentData(bool reqErrorFlag, String jsonData)
{
  displayHeader(reqErrorFlag);
  String dataLabel = "";
  
  getCurrentData(jsonData);
  
  tft.setCursor(LINE_BEGIN_X, TOP_DATA_LINE);
  JSONVar myArray = JSON.parse(jsonData);
  int cursorPosn = 0;
  int lineNumber = 0;
  for (int i=0; i<myArray[0].length(); i++)
  {
    lineNumber = i;
    switch(i)
    {
      
      case WIND_SPEED:
        tft.fillRect(tft.getCursorX(), tft.getCursorY(), tft.width(), LINE_SPACE, MY_BLACK);
        tft.setTextColor(_normalTextClr, MY_BLACK);
        dataLabel = "Wind spd [mph]: ";
        tft.print(dataLabel); 
        tft.setTextColor(_outsideTextClr, MY_BLACK);
        if(String(myArray[0][WIND_SPEED])=="0.0")
        {
          tft.print("< 1.0");
        }
        else
        {
          tft.print(String(myArray[0][WIND_SPEED]));
          float newWindSpd = String(myArray[0][WIND_SPEED]).toFloat();
          if (newWindSpd > maxWindSpeed)
          {
            maxWindSpeed = newWindSpd;
          }
                  
        }
        tft.setTextColor(_normalTextClr, MY_BLACK);
        break;
        
      case WIND_DIRECTION:
        _windDirn = String(myArray[0][WIND_DIRECTION]);
        displayDataLine("Wind direction: ", _windDirn, "", _normalTextClr, _outsideTextClr, _insideTextClr);
        if (_windDirn == "N")
        {
          windDirCount[NORTH] += 1; 
        }
        else if (_windDirn == "NE")
             {
               windDirCount[NORTHEAST] += 1;
             }
             else if (_windDirn == "E")
                  {
                    windDirCount[EAST] += 1;
                  }
                  else if (_windDirn == "SE")
                       {
                          windDirCount[SOUTHEAST] += 1;
                       } 
                       else if (_windDirn == "S")
                            {
                              windDirCount[SOUTH] += 1;
                            } 
                            else if (_windDirn == "SW")
                                 {
                                   windDirCount[SOUTHWEST] += 1;
                                 } 
                                 else if (_windDirn == "W")
                                      {
                                        windDirCount[WEST] += 1;
                                      }  
                                      else if (_windDirn == "NW")
                                           {
                                             windDirCount[NORTHWEST] += 1;
                                           } 
        calculateDirectionPct();                                               
        
        break;
        
      case WIND_CHILL:
        displayDataLine("Wind chill: ", String(myArray[0][WIND_CHILL]), "", _normalTextClr, _outsideTextClr, _insideTextClr);
        break;
      
      case TEMP:
      {
        displayDataLine("Temp [degF]:   ", String(myArray[0][TEMP]), sInsideTemperature, _normalTextClr, _outsideTextClr, _insideTextClr);

         
        float newTemp = String(myArray[0][TEMP]).toFloat();
        
        if (newTemp > maxTemp)
        {
          maxTemp = newTemp;
          maxTempTime = tellTime(false);
        }
        if (newTemp < minTemp)
        {
          minTemp = newTemp;
          minTempTime = tellTime(false);
        }
        if (fInsideTemperature > maxTempIn)
        {
          maxTempIn = fInsideTemperature;
          maxTempTimeIn = tellTime(false);
        }
        if (fInsideTemperature < minTempIn)
        {
          minTempIn = fInsideTemperature;
          minTempTimeIn = tellTime(false);
        }
        
        break;
      }
      
      case HUM:
        displayDataLine("Rel hum [%]:   ", String(myArray[0][HUM]), sInsideHumidity, _normalTextClr, _outsideTextClr, _insideTextClr);
        break;  
      
      case DEWPOINT:
        displayDataLine("Dew pt [degF]: ", String(myArray[0][DEWPOINT]), "", _normalTextClr, _outsideTextClr, _insideTextClr);
        break;
      
      case HEAT_INDEX:
        displayDataLine("Heat idx [degF]: ", String(myArray[0][HEAT_INDEX]), "", _normalTextClr, _outsideTextClr, _insideTextClr);
        break;
      
      default:
        break;
      
    }

    cursorPosn = TOP_DATA_LINE+LINE_SPACE + i * LINE_SPACE;
    tft.setCursor(LINE_BEGIN_X, cursorPosn);

  } // end for loop

  displayDataLine("Stn prs [mb]: ", "", sBaroPressure, _normalTextClr, _outsideTextClr, _insideTextClr);



}

String httpGETRequest(const char* url) 
{
  WiFiClient client;
  HTTPClient http;

  requestCount += 1;

  // IP address with path or Domain name with URL path 
  http.begin(client, url);
  
  // Send HTTP GET request
  int httpResponseCode = http.GET();
  
  String payload = "--"; 
  
  if (httpResponseCode>0) 
  {
    successCount += 1;
    payload = http.getString();
  }
  else 
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
} 

// Return a string with hh:mm in 24-hour format 
String tellTime(bool minutesOnly)
{
  Wire.begin(12,4);
  rtc.begin();
  DateTime now = rtc.now();  
  String sHour = String(now.hour());
  String sMinute = String(now.minute());
  String sSecond = String(now.second());
  if (now.minute() < 10)
  {
    sMinute = "0" + sMinute;
  }
  if (now.second() < 10)
  {
    sSecond = "0" + sSecond;
  }
  
  String sTime = sHour+":"+sMinute; // +":"+sSecond;
  if (minutesOnly)
  {
    sTime = sTime.substring(3);
  }
  // Serial.println("Just minutes "+sTime.substring(3));
  return sTime;
}

float getTempHumidityFloat(int choice)
{
  Wire.begin(12,4);
  sht31.begin(0x44);
  float value;
  if (choice == TEMPERATURE)
  {
    value = sht31.readTemperature();
    value = (value * 1.8) + 32;
  }
  else
  {
    value = sht31.readHumidity();
  }
  return value;
}

String getTempHumidity(int choice)
{
  float value = getTempHumidityFloat(choice);
  char cValue[7];
  dtostrf(value, 5,1,cValue);

  return String(cValue);
}

String getBarometricPressure()
{
  Wire.begin(12,4);
  baro.begin();
  float pressure = baro.getPressure();
  float altitude = baro.getAltitude();
  float temperature = baro.getTemperature();
  float value = pressure;

  char cValue[7];
  dtostrf(value, 5,1,cValue);

  return String(cValue);

}

void displayTime(int lineXCoord, int lineYCoord)
{
  tft.fillRect(0, lineYCoord, tft.width(), LINE_SPACE, MY_BLACK);
  tft.setCursor(0, lineYCoord);
  tft.setTextColor(_normalTextClr, MY_BLACK);
  tft.print(tellTime(false));
}

void displayDataLine(String dataLabel, String sValue1, String sValue2, int _normalTextClr, int _outsideTextClr, int _insideTextClr)
{
  tft.fillRect(tft.getCursorX(), tft.getCursorY(), tft.width(), LINE_SPACE, MY_BLACK);
  tft.print(dataLabel);
  if (sValue1.length()>0)
  { 
    tft.setTextColor(_outsideTextClr, MY_BLACK);
    tft.print(sValue1);
    tft.setTextColor(_normalTextClr, MY_BLACK);
  }
  if (sValue2.length()>0)
  { 
    tft.setTextColor(_insideTextClr, MY_BLACK);
    tft.print(" "+sValue2);
    tft.setTextColor(_normalTextClr, MY_BLACK);
  }
}

void updateStatsLine(bool reqErrorFlag)
{
  if (requestCount != 0)
  {
    if (reqErrorFlag)
    {
      tft.setTextColor(MY_RED, MY_BLACK); 
    }
    else
    {
      tft.setTextColor(_normalTextClr, MY_BLACK); 
    }

    float successPct = (float)successCount/ (float)requestCount * 100;
    char dispVal[7];
    dtostrf(successPct, 5,1,dispVal);
    String sSuccess = "Req stats: " + String(dispVal) + "% [" + String(requestCount) +"]";
    tft.setCursor(LINE_BEGIN_X, 20);               
    tft.print(sSuccess);

    // Be sure we print white from now on in case it was set to red due to an error
    tft.setTextColor(_normalTextClr, MY_BLACK);
  }

}


void calculateDirectionPct()
{

  int totalMeasurements = 0;
  for (int idx=0; idx<8; idx++)
  {
    totalMeasurements += windDirCount[idx];  
  }

  
  if (totalMeasurements != 0)
  {
    for (int idx=0; idx<8; idx++)
    {
      float aPct = (float)windDirCount[idx]/(float)totalMeasurements* 100.0;
      char dispVal[7];
      dtostrf(aPct, 5,0,dispVal);
      String sDispVal = String(dispVal);
      sDispVal.trim();
      
      if (sDispVal.length()==1)
      {
        sDispVal = " "+sDispVal;
      }
      windDirectionPcts[idx] = sDispVal;
    } 
  }
  
}

void displayHeader(bool reqErrorFlag)
{
  tft.fillScreen(MY_BLACK); 
  tft.drawFastHLine(LINE_BEGIN_X, 34,  tft.width(), MY_BLUE); 
  tft.setTextColor(_normalTextClr, MY_BLACK);     
  tft.setTextSize(1);                
  tft.setCursor(LINE_BEGIN_X, FIRST_LINE);              
  tft.print("S. S. Minnow Weather Stn");
  
  displayTime(LINE_BEGIN_X, FIRST_LINE+LINE_SPACE);
  updateStatsLine(reqErrorFlag);
        
}

void setup(void)
{
  Serial.begin(115200);
  Wire.begin(12,4);
  Serial.println();
  Serial.println("Version number: "+String(VERSION_NUMBER));

  // Bring up the real time clock
  if (! rtc.begin()) 
  {
      Serial.println("Couldn't find RTC");
      while (1) delay(10);
  }
  else
  {
    Serial.println("RTC started");
  }

  // Bring up the t & h sensor
  if (! sht31.begin(0x44)) 
  {   
    Serial.println("Couldn't find SHT31 temp/humidity sensor");
    while (1) delay(10);
  }
  else
  {
    Serial.println("T & H sensor started");
  }

  // Bring up the barometric pressure sensor
  if (!baro.begin()) 
  {
      Serial.println("Could not find barometric pressure sensor.");
      while(1) delay(10);
  }
  else
  {
    Serial.println("Barometric pressure sensor started");
    
    // use to set sea level pressure for current location
    // this is needed for accurate altitude measurement
    // STD SLP = 1013.26 hPa
    baro.setSeaPressure(1013.26);
  }

  // Bring up the port extender for options switches access
  if (!mcp.begin_I2C()) 
  {
    Serial.println("Could not start the MCP23017 port extender");
    while (1);
  }
  else
  {
    Serial.println("MCP23017 port extender started");
    
    // Configure switch pins for input with pull up
    // When switches are 'on' they return false, otherwise true
    mcp.pinMode(SW_0_PIN, INPUT_PULLUP);
    mcp.pinMode(SW_1_PIN, INPUT_PULLUP);
  }

  tft.initR(INITR_GREENTAB);     
  tft.setRotation(3); // Landscape mode
  
  tft.fillScreen(MY_BLACK);  // erase screen
  tft.setCursor(LINE_BEGIN_X, TOP_DATA_LINE);              
  
  WiFi.begin(ssid, password);
  tft.print("Connecting..");
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    tft.print(".");
  }
  tft.print("Connected to S.S. Minnow");
  
  delay(2000);
  tft.fillRect(0, TOP_DATA_LINE, tft.width(), tft.height()-TOP_DATA_LINE, MY_BLACK);
 
}

void showHistoryData()
{
  tft.fillRect(LINE_BEGIN_X, FIRST_LINE, tft.width(), tft.height()-FIRST_LINE, MY_BLACK);
  
  int cursorPosn = FIRST_LINE;
  tft.setTextColor(_normalTextClr, MY_BLACK);
  tft.setCursor(LINE_BEGIN_X, cursorPosn);
  tft.print("           History");
  cursorPosn += LINE_SPACE;
  tft.drawFastHLine(LINE_BEGIN_X, cursorPosn,  tft.width(), MY_BLUE); 

  cursorPosn += LINE_SPACE;
  tft.setCursor(LINE_BEGIN_X, cursorPosn);
  tft.print("       Min         Max");
  cursorPosn += LINE_SPACE; 
  tft.setCursor(LINE_BEGIN_X, cursorPosn);
  tft.print("To:");
  tft.setTextColor(_normalTextClr, MY_BLACK);
  tft.print(String(minTemp)+" ");
  tft.print(String(minTempTime)+" "); 
  tft.setTextColor(_normalTextClr, MY_BLACK);       
  tft.print(String(maxTemp)+" ");
  tft.print(String(maxTempTime));

  cursorPosn += LINE_SPACE; 
  tft.setCursor(LINE_BEGIN_X, cursorPosn);
  tft.setTextColor(_normalTextClr, MY_BLACK);
  tft.print("Ti:");
  tft.setTextColor(_normalTextClr, MY_BLACK);
  tft.setTextColor(_normalTextClr, MY_BLACK);
  tft.print(String(minTempIn)+" ");
  tft.print(String(minTempTimeIn)+" "); 
  tft.setTextColor(_normalTextClr, MY_BLACK);       
  tft.print(String(maxTempIn)+" ");
  tft.print(String(maxTempTimeIn));
  tft.setTextColor(_normalTextClr, MY_BLACK);

  cursorPosn += LINE_SPACE;
  tft.setCursor(LINE_BEGIN_X, cursorPosn);
  tft.setTextColor(_normalTextClr, MY_BLACK);
  tft.print("Max wind: ");
  tft.setTextColor(_normalTextClr, MY_BLACK);
  tft.print(String(maxWindSpeed));

  
  cursorPosn += LINE_SPACE;
  tft.setCursor(LINE_BEGIN_X, cursorPosn);
  tft.setTextColor(_normalTextClr, MY_BLACK);
  
  tft.print(" N  -  E  -  S  -  W  -");
  
  cursorPosn += LINE_SPACE;
  tft.setCursor(LINE_BEGIN_X, cursorPosn);
  for(int idx=0; idx<8; idx++)
  {
    tft.print(windDirectionPcts[idx]+" ");
  }

}

void showKeyData(String jsonData)
{
  getCurrentData(jsonData);
  
  // Display inside and outside temps in large numbers
  tft.fillScreen(MY_BLACK);  // erase screen
  tft.setTextColor(MY_YELLOW, MY_BLACK);     
                  
  tft.setCursor(LINE_BEGIN_X, FIRST_LINE+3*LINE_SPACE);
  tft.setTextSize(2);
  tft.print("  ");              
  tft.setTextSize(10);
  tft.print(_currentInTemp);
  delay(7500);
  
  tft.fillScreen(MY_BLACK);  
  tft.setTextColor(MY_GREEN, MY_BLACK);     
  tft.setCursor(LINE_BEGIN_X, FIRST_LINE+3*LINE_SPACE);              
  tft.setTextSize(2);
  tft.print("  ");              
  tft.setTextSize(10);                
  tft.print(_currentOutTemp);
  delay(7500);

  tft.fillScreen(MY_BLACK);  
  tft.setTextColor(MY_WHITE, MY_BLACK);     
  tft.setCursor(LINE_BEGIN_X, FIRST_LINE+3*LINE_SPACE);              
  tft.setTextSize(2);
  tft.print("  ");              
  tft.setTextSize(10);                
  tft.print(_windDirn);
  delay(7500);


  tft.setTextColor(_normalTextClr, MY_BLACK);
  tft.setTextSize(1);
}

void loop()
{
  String jsonData;
  String dataLabel = "";
  bool reqErrorFlag = false;
  bool firstPass = true;
  static bool historyShownFlag = false;
  static unsigned long previousMillis = 0;
    
  unsigned long currentMillis = millis();

  // Switches 'on' means grounded pins, negative logic
  bool switch0 = !mcp.digitalRead(SW_0_PIN);
  bool switch1 = !mcp.digitalRead(SW_1_PIN);
  bool showOnlyKeyData = false;
  
  if (switch0)
  {
    // Switch 0 is on - only show big numbers
    showOnlyKeyData = true;
  }
  else
  {
    showOnlyKeyData = false;
  }

  if (switch1)
  {
    _isNightMode = true;
  }
  else
  {
    _isNightMode = false;
  }
  
  if (_isNightMode)
  {
    _normalTextClr = MY_RED;
    _outsideTextClr = MY_RED;
    _insideTextClr = MY_RED;
  }
  else
  {
    _normalTextClr = MY_GREEN;
    _outsideTextClr = MY_GREEN;
    _insideTextClr = MY_GREEN;
  }   

  // The interval approach to doing things in the loop keeps
  // processing somewhat separate from however many delay() calls
  // occur in the loop
  if((currentMillis - previousMillis >= interval) || firstPass)
  {
    firstPass = false;
    historyShownFlag = false;
    tft.setCursor(LINE_BEGIN_X, TOP_DATA_LINE); 
    tft.setTextColor(_normalTextClr);             

     // Check WiFi connection status
    if ((WiFiMulti.run() == WL_CONNECTED)) 
    {
      jsonData = httpGETRequest(jsonURL);
      if (jsonData != "--")
      {
        reqErrorFlag = false;
      }
      else
      {
        reqErrorFlag = true;    
      }
      if (!reqErrorFlag)
      {
        if (!showOnlyKeyData)
        {
          showCurrentData(reqErrorFlag, jsonData);
          delay(15000);
        }
      }
      reqErrorFlag = false;

      
      // Show history data screen
      if ( (minTempTime != "") && !historyShownFlag && !firstPass)  
      {
        if (!showOnlyKeyData)
        {
          showHistoryData();
          delay(15000);
          historyShownFlag = true;
        }
      }

      showKeyData(jsonData);
      
        
      previousMillis = currentMillis;
    } // end if WL_CONNECTED
    else 
    {
      Serial.println("WiFi Disconnected");
    }
  } // end interval check

} // end loop()
