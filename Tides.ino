#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <time.h>

#include <ArduinoJson.h>

#include <U8g2lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

WiFiClient client; //Use HTTP
char server[] = "tidesandcurrents.noaa.gov";

//Use webUnixTime to get time
uint16_t lastWebUnixTimeYear;
uint8_t lastWebUnixTimeMonth;
uint8_t lastWebUnixTimeDay;
unsigned long lastWebUnixTimeMillis = 0;
unsigned long lastWebServiceTimeMillis = 0;
unsigned long webTime = 0;
unsigned long refreshWebTimeInveralSeconds = 864000 - 23; //once a day, offset by a small value to avoid always checking at the same time 
unsigned long refreshWebServiceInveralSeconds = 60*60;

unsigned long nextTidePeakTimeStamp = 0;

String station = "9449679"; //Noaa.gov station

//Use a counter so that we can retry getting data once, but not everytime. Don't want to DDoS the server
int dataExpired = 0;

int refreshIntervalSeconds = 15;
int loop_count = 0;
int webTimeRequestCount = 0;
int webServiceRequestCount = 0;


//For displaying data
int tide_direction = 0;
int x = 0; // percentage of time between peaks
double v1 = 0.0; //tide before now
double v2 = 0.0; // tide after now
String t1 = "";
String t2 = "";

boolean debug = false;

//https://robotzero.one/heltec-wifi-kit-32/
// the OLED used
U8G2_SSD1306_128X32_UNIVISION_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 5, /* data=*/ 4, /* reset=*/ 16);

// Allocate JsonBuffer
// Use arduinojson.org/assistant to compute the capacity.
const size_t bufferSize = JSON_ARRAY_SIZE(8) + JSON_OBJECT_SIZE(1) + 8*JSON_OBJECT_SIZE(3) + 320;
DynamicJsonBuffer jsonBuffer(bufferSize);


/*
 * From https://github.com/PaulStoffregen/Time/blob/master/Time.cpp
 * 
*/
#define SECS_PER_MIN  ((time_t)(60UL))
#define SECS_PER_HOUR ((time_t)(3600UL))
#define SECS_PER_DAY  ((time_t)(SECS_PER_HOUR * 24UL))
#define LEAP_YEAR(Y)     ( ((1970+(Y))>0) && !((1970+(Y))%4) && ( ((1970+(Y))%100) || !((1970+(Y))%400) ) )
static const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31}; // API starts months from 1, this array starts from 0

time_t makeTime(uint8_t Year, uint8_t Month, uint8_t Day, uint8_t Hour, uint8_t Minute, uint8_t Second = 0){   
// assemble time elements into time_t 
// note year argument is offset from 1970 (see macros in time.h to convert to other formats)
// previous version used full four digit year (or digits since 2000),i.e. 2009 was 2009 or 9

  if (debug){
  Serial.print("year=");Serial.println(Year);
  Serial.print("Month=");Serial.println(Month);
  Serial.print("Day=");Serial.println(Day);
  Serial.print("Hour=");Serial.println(Hour);
  Serial.print("Minute=");Serial.println(Minute);
  Serial.print("Second=");Serial.println(Second);
  }
  
  int i;
  uint32_t seconds;

  // seconds from 1970 till 1 jan 00:00:00 of the given year
  seconds= Year*(SECS_PER_DAY * 365);
  for (i = 0; i < Year; i++) {
    if (LEAP_YEAR(i)) {
      seconds +=  SECS_PER_DAY;   // add extra days for leap years
    }
  }
  
  // add days for this year, months start from 1
  for (i = 1; i < Month; i++) {
    if ( (i == 2) && LEAP_YEAR(Year)) { 
      seconds += SECS_PER_DAY * 29;
    } else {
      seconds += SECS_PER_DAY * monthDays[i-1];  //monthDay array starts from 0
    }
  }
  seconds+= (Day-1) * SECS_PER_DAY;
  seconds+= Hour * SECS_PER_HOUR;
  seconds+= Minute * SECS_PER_MIN;
  seconds+= Second;
  return (time_t)seconds; 
}

//CS helper function to return unix time stamp from YYYY-MM-DD HH:SS formatted char string (c str)
time_t makeTime(String t, int setT1OrT2 = 0) {  
  String yearStr = "";
  yearStr += t.charAt(0);
  yearStr += t.charAt(1);
  yearStr += t.charAt(2);
  yearStr += t.charAt(3);
  uint8_t year = yearStr.toInt() - 1970; //Year is offset from 1970
  
  //Build a string with charAt.  Other methods didn't return the correct value (e.g. substring)
  String monthStr = "";
  monthStr += t.charAt(5);
  monthStr += t.charAt(6);
  uint8_t month = monthStr.toInt();
  
  String dayStr = "";
  dayStr += t.charAt(8);
  dayStr += t.charAt(9);
  uint8_t day = dayStr.toInt();
  
  String hourStr = "";
  hourStr += t.charAt(11);
  hourStr += t.charAt(12);
  uint8_t hour = hourStr.toInt();
  
  String minuteStr = "";
  minuteStr += t.charAt(14);
  minuteStr += t.charAt(15);
  uint8_t minute = minuteStr.toInt();

  uint8_t second = 0;

  if (setT1OrT2 == 1) {
    t1 = t.charAt(11);
    t1 += t.charAt(12);
    t1 += t.charAt(13);
    t1 += t.charAt(14);
    t1 += t.charAt(15);
  } else if (setT1OrT2 == 2) {
    t2 = t.charAt(11);
    t2 += t.charAt(12);
    t2 += t.charAt(13);
    t2 += t.charAt(14);
    t2 += t.charAt(15);    
  }

  time_t rtn = makeTime(year, month, day, hour, minute, second);
  
  return rtn;
}

void setup()
{
  Serial.begin(115200);
  
  u8g2.begin();
  u8g2.setFont(u8g2_font_chikita_tf);

  const char* apName = "AP-NAME";
  const char* password = "tidespassword";

  u8g2.drawStr(0,16,"Please connect & configure");
  u8g2.drawStr(0,23,"SSID:");
  u8g2.drawStr(30,23, apName);
  u8g2.drawStr(0,30,"Pass:"); 
  u8g2.drawStr(30,30, password); 

  u8g2.sendBuffer();
  
  WiFiManager wifiManager;  
  wifiManager.autoConnect(apName, password);

  u8g2.sendBuffer();
  
  delay(1000);

  //Set time
  webUnixTime(client);
  
  Serial.print("Time: ");
  Serial.println(webTime + lastWebUnixTimeMillis/1000);

  u8g2.drawStr(0,30,"Setup done");
  
  Serial.println("Setup done");
  
  u8g2.sendBuffer();

  delay(1000);
  
  httpsRequest();
}


// this method makes a HTTP connection to the server:
// Modified from https://arduinojson.org/example/http-client/
void httpsRequest() {
  // close any connection before send a new request.
  // This will free the socket on the WiFi shield
  //clientSSL.stop();
  
  WiFiClientSecure clientSSL; //Use HTTPS
  
  clientSSL.setTimeout(10000);

  // if there's a successful connection:
  if (clientSSL.connect(server, 443)) {
    webServiceRequestCount++;
    time_t now = webTime + (millis() - lastWebUnixTimeMillis)/1000;
    //Serial.print("day(now): "); Serial.print(day(now)); 
    Serial.print(" now: ");  Serial.println(now);

    //The loop below assumes that there is at least one record before now.
    // If this isn't the case then we can't calulate. 
    //TODO: Set beginDate to the previous day if webTime or just display without the starting time
    String beginDate = String(lastWebUnixTimeYear);
    String month = String(lastWebUnixTimeMonth); if (month.length() == 1) month = "0" + month;
    String day = String(lastWebUnixTimeDay); if (day.length() == 1) day = "0" + day;
    beginDate += month;
    beginDate += day;

    String endDate = "";

    //If this is Dec 31st next day is Jan 1
    if (lastWebUnixTimeDay == 31 && lastWebUnixTimeMonth == 12) {
      endDate += String(lastWebUnixTimeYear+1);
      endDate += "0101";
    } else {
      endDate += String(lastWebUnixTimeYear);

      //If this is past the 28 then just set the end date to the 1st of the next month
      if (lastWebUnixTimeDay > 28) {
        month = String(lastWebUnixTimeMonth + 1); if (month.length() == 1) month = "0" + month;
        endDate += month;
        endDate += "01";
      } else { //Just a day
        endDate += month;
        day = String(lastWebUnixTimeDay + 1); if (day.length() == 1) day = "0" + day;
        endDate += day;
      }
    }

    String getStr = "GET /api/datagetter?product=predictions&application=NOS.COOPS.TAC.WL&begin_date=";
    getStr += beginDate;
    getStr += "&end_date=";
    getStr += endDate;
    getStr += "&datum=MLLW&station=";
    getStr += station;
    getStr += "&time_zone=gmt&units=english&interval=hilo&format=json HTTP/1.1";
    
    Serial.print("connecting... sending: "); Serial.println(getStr);
    // send the HTTP PUT request:
    clientSSL.println(getStr);
    clientSSL.println("Host: noaa.gov");
    clientSSL.println("User-Agent: ArduinoWiFi/1.1");
    clientSSL.println("Connection: close");
    
    if (clientSSL.println() == 0) {
      Serial.println(F("Failed to send request"));
      return;
    }

    // Check HTTP status
    char status[32] = {0};
    clientSSL.readBytesUntil('\r', status, sizeof(status));
    if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
      Serial.print(F("Unexpected response: "));
      Serial.println(status);
      return;
    }

    // Skip HTTP headers
    char endOfHeaders[] = "\r\n\r\n";
    if (!clientSSL.find(endOfHeaders)) {
      Serial.println(F("Invalid response"));
      return;
    }
    
    //const char* json = "{\"predictions\":[{\"t\":\"2018-05-27 03:56\",\"v\":\"9.180\",\"type\":\"H\"},{\"t\":\"2018-05-27 11:05\",\"v\":\"-0.277\",\"type\":\"L\"},{\"t\":\"2018-05-27 18:09\",\"v\":\"8.244\",\"type\":\"H\"},{\"t\":\"2018-05-27 23:03\",\"v\":\"5.354\",\"type\":\"L\"},{\"t\":\"2018-05-28 04:26\",\"v\":\"8.971\",\"type\":\"H\"},{\"t\":\"2018-05-28 11:40\",\"v\":\"-0.799\",\"type\":\"L\"},{\"t\":\"2018-05-28 18:58\",\"v\":\"8.788\",\"type\":\"H\"},{\"t\":\"2018-05-28 23:55\",\"v\":\"5.836\",\"type\":\"L\"}]}";
    jsonBuffer.clear();
    JsonObject& root = jsonBuffer.parseObject(clientSSL);
    
    if (!root.success()) {
      Serial.println(F("Parsing failed!"));
      return;
    }
    
    // Disconnect  
    delay(10);
    clientSSL.flush();
    clientSSL.stop();
    lastWebServiceTimeMillis = millis();

    //Set to 0. If this value is not set
    tide_direction = 0;
    
    //Set tide_direction and x    
    for (size_t i=0; i < sizeof(root["predictions"]); i++) {
      //t is in format: 2018-05-29 04:56
      String t = root["predictions"][i]["t"].as<char*>();
      time_t iUnixTimeStamp = makeTime(t, 2);

      if (now <= iUnixTimeStamp) {
        Serial.print("match! now="); Serial.print(now); Serial.print(" iUnixTimeStamp=");Serial.println(iUnixTimeStamp);
        Serial.print("match! i="); Serial.print(i); Serial.print(" type=");Serial.print(root["predictions"][i]["type"].as<char*>());Serial.print(" v="); Serial.println(root["predictions"][i]["v"].as<char*>());

        String type = root["predictions"][i]["type"].as<char*>();
        
        if(type.equals("L"))
          tide_direction = -1; // tide going down
        else
          tide_direction = 1; // tide going up

        //Get next index 
        if (i == 0) // There's no tide record before now.
          Serial.println("ERROR SHOULDN'T happen");
        else {
          time_t i1 = makeTime(root["predictions"][i-1]["t"].as<char*>(), 1);
          
          v1 = atof(root["predictions"][i-1]["v"].as<char*>());
          v2 = atof(root["predictions"][i]["v"].as<char*>());

          Serial.print(" i1= "); Serial.println(i1);

          x = round(100*(now - i1)/(iUnixTimeStamp - i1));
        }

        break;
      } else {
        Serial.print(" NOT match. now="); Serial.print(now); Serial.print(" iUnixTimeStamp=");Serial.println(iUnixTimeStamp);
        Serial.print(" NOT match i="); Serial.print(i); Serial.print(" type=");Serial.print(root["predictions"][i]["type"].as<char*>());Serial.print(" v="); Serial.println(root["predictions"][i]["v"].as<char*>());
      }
    }

    //deallocate so we don't get a stack overflow
  } else {
    // if you couldn't make a connection:
    Serial.println("httpRequest connection failed");
  }
}


void display_data(void) {  
  u8g2.clearBuffer();

  u8g2.setFontMode(1);
  u8g2.setCursor(0,30);
  
  //char* v1 = prediction1["v"].as<char*>();
  //double v1 = atof();
  //Serial.print("v1 = "); Serial.println(v1);
    
  if (tide_direction < 0) {
    //u8g2.print("Tide going down");
    u8g2.drawLine(1, 1, 100, 20);
  } else {
    //u8g2.print("Tide going up");
    u8g2.drawLine(1, 20, 100, 1);
  }

  u8g2.drawLine(x, 1, x, 20);

  if (webTime == 0) {
    Serial.println("Get webUnixTime in display_data");
    webUnixTime(client);
  }

  //Build string in format: low tide 3:40pm (0.07ft), high tide 11:25pm (8.38ft)
  String s = "";
  //s += (webTime + (millis() - lastWebUnixTimeMillis)/1000);

  if (tide_direction == 0) {
    s = "Cannot get tidal info.";
  }else {
    if (tide_direction > 0)
      s = "low ";
    else
      s = "high ";
  
    //s += t1; //TODO: Convert to local time
    s += " (";
    s += v1;
    s += "), ";
  
    if (tide_direction > 0)
      s += "high ";
    else
      s += "low ";
  
    //s += t2;
    s += " (";
    s += v2;
    s += ")";
  }
  
  u8g2.drawStr(0, 30, s.c_str());
  
  u8g2.sendBuffer();
}


void loop()
{ 
  loop_count++;
  Serial.print("webTime before: ");
  Serial.print(webTime);
  Serial.print(" lastWebUnixTimeMillis: ");
  Serial.print(lastWebUnixTimeMillis);
  Serial.print(" lastWebServiceTimeMillis: ");
  Serial.print(lastWebServiceTimeMillis);
  Serial.print(" millis(): ");
  Serial.println(millis());
  
  if (webTime == 0 || (millis() - lastWebUnixTimeMillis)/1000 > refreshWebTimeInveralSeconds) {
    Serial.println("Get webUnixTime before");
    webUnixTime(client);
    Serial.println("Get webUnixTime after");
  }
  
  if (nextTidePeakTimeStamp < (webTime + (millis() - lastWebUnixTimeMillis)/1000)) {
    dataExpired++;

    if (dataExpired > 60) //60 interations
      dataExpired = 1; //retry

    u8g2.setCursor(102,28);
    u8g2.print(dataExpired);
  } else
    dataExpired = 0;
  
  
  if (webTime > 0 && ((millis() - lastWebServiceTimeMillis)/1000 > refreshWebServiceInveralSeconds 
                      || dataExpired == 1)) {
    Serial.println("before httpsRequest()");
    httpsRequest();
    Serial.println("after httpsRequest()");
  }

  display_data();
  
  u8g2.setCursor(102,7);
  u8g2.print(loop_count);
  u8g2.setCursor(102,14);
  u8g2.print(webTimeRequestCount);
  u8g2.setCursor(102,21);
  u8g2.print(webServiceRequestCount);
  u8g2.sendBuffer();
  
  delay(refreshIntervalSeconds*1000);
}

/*
 * © Francesco Potortì 2013 - GPLv3
 *
 * Send an HTTP packet and wait for the response, return the Unix time
 * http://playground.arduino.cc//Code/Webclient
 */

unsigned long webUnixTime (Client &client)
{
  // Just choose any reasonably busy web server, the load is really low
  if (client.connect("google.com", 80))
    {
      webTimeRequestCount++;
      // Make an HTTP 1.1 request which is missing a Host: header
      // compliant servers are required to answer with an error that includes
      // a Date: header.
      client.print(F("GET / HTTP/1.1 \r\n\r\n"));

      char buf[5];      // temporary buffer for characters
      client.setTimeout(5000);
      if (client.find((char *)"\r\nDate: ") // look for Date: header
    && client.readBytes(buf, 5) == 5) // discard
  {
    unsigned day = client.parseInt();    // day
    
    lastWebUnixTimeDay = day;      
    Serial.print("web day: "); Serial.println(lastWebUnixTimeDay);
    
    client.readBytes(buf, 1);    // discard
    client.readBytes(buf, 3);    // month
    int year = client.parseInt();    // year
    lastWebUnixTimeYear = year; Serial.print("web year: "); Serial.println(lastWebUnixTimeYear);
    byte hour = client.parseInt();   // hour
    byte minute = client.parseInt(); // minute
    byte second = client.parseInt(); // second

    Serial.print("Time from HTTP GET: ");
    Serial.print(year);
    Serial.print(buf);
    Serial.print(day);
    Serial.print(" ");
    Serial.print(hour);
    Serial.print(":");
    Serial.print(minute);
    Serial.print(":");
    Serial.println(second);
    
    int daysInPrevMonths;
    switch (buf[0])
      {
      case 'F': daysInPrevMonths =  31; lastWebUnixTimeMonth = 2; break; // Feb
      case 'S': daysInPrevMonths = 243; lastWebUnixTimeMonth = 9; break; // Sep
      case 'O': daysInPrevMonths = 273; lastWebUnixTimeMonth = 10; break; // Oct
      case 'N': daysInPrevMonths = 304; lastWebUnixTimeMonth = 11; break; // Nov
      case 'D': daysInPrevMonths = 334; lastWebUnixTimeMonth = 12; break; // Dec
      default:
        if (buf[0] == 'J' && buf[1] == 'a') {
          daysInPrevMonths = 0; 
          lastWebUnixTimeMonth = 1;  // Jan
        } else if (buf[0] == 'A' && buf[1] == 'p') {
          daysInPrevMonths = 90;  
          lastWebUnixTimeMonth = 4; // Apr
        } else 
        switch (buf[2])
         {
         case 'r': daysInPrevMonths =  59; lastWebUnixTimeMonth = 3; break; // Mar
         case 'y': daysInPrevMonths = 120; lastWebUnixTimeMonth = 5; break; // May
         case 'n': daysInPrevMonths = 151; lastWebUnixTimeMonth = 6; break; // Jun
         case 'l': daysInPrevMonths = 181; lastWebUnixTimeMonth = 7; break; // Jul
         default: // add a default label here to avoid compiler warning
         case 'g': daysInPrevMonths = 212; lastWebUnixTimeMonth = 8; break; // Aug
         }
      }

    Serial.print("web month: "); Serial.println(lastWebUnixTimeMonth);
    
    // This code will not work after February 2100
    // because it does not account for 2100 not being a leap year and because
    // we use the day variable as accumulator, which would overflow in 2149
    day += (year - 1970) * 365; // days from 1970 to the whole past year
    day += (year - 1969) >> 2;  // plus one day per leap year 
    day += daysInPrevMonths;  // plus days for previous months this year
    if (daysInPrevMonths >= 59  // if we are past February
        && ((year & 3) == 0)) // and this is a leap year
      day += 1;     // add one day
    // Remove today, add hours, minutes and seconds this month
    webTime = (((day-1ul) * 24 + hour) * 60 + minute) * 60 + second;
    lastWebUnixTimeMillis = millis(); //reset last time we got the webUnixTiem
  }
    }
  delay(10);
  client.flush();
  client.stop();

  return webTime;
}


