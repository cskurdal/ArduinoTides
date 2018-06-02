#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

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
unsigned long lastWebUnixTimeMillis = 0;
unsigned long webTime = 0;
unsigned long refreshWebTimeInveralSeconds = 864000 - 23; //once a day, offset by a small value to avoid always checking at the same time 
int refreshIntervalSeconds = 15;
int loop_count = 0;

// Allocate JsonBuffer
// Use arduinojson.org/assistant to compute the capacity.
const size_t bufferSize = JSON_ARRAY_SIZE(8) + JSON_OBJECT_SIZE(1) + 8*JSON_OBJECT_SIZE(3) + 320;
DynamicJsonBuffer jsonBuffer(bufferSize);


/*
 * From https://github.com/PaulStoffregen/Time/blob/master/Time.cpp
 * 
*/
#define SECS_PER_MIN  ((uint32_t)(60))
#define SECS_PER_HOUR ((uint32_t)(3600))
#define SECS_PER_DAY  ((uint32_t)(SECS_PER_HOUR * 24))
#define LEAP_YEAR(Y)     ( ((1970+(Y))>0) && !((1970+(Y))%4) && ( ((1970+(Y))%100) || !((1970+(Y))%400) ) )
static  const uint32_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31}; // API starts months from 1, this array starts from 0

uint32_t makeTime(uint32_t Year, uint32_t Month, uint32_t Day, uint32_t Hour, uint32_t Minute, uint32_t Second = 0){   
// assemble time elements into time_t 
// note year argument is offset from 1970 (see macros in time.h to convert to other formats)
// previous version used full four digit year (or digits since 2000),i.e. 2009 was 2009 or 9

  Serial.print("year=");Serial.println(Year);
  Serial.print("Month=");Serial.println(Month);
  Serial.print("Day=");Serial.println(Day);
  Serial.print("Hour=");Serial.println(Hour);
  Serial.print("Minute=");Serial.println(Minute);
  Serial.print("Second=");Serial.println(Second);
  
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
  return seconds; 
}


//https://robotzero.one/heltec-wifi-kit-32/
// the OLED used
U8G2_SSD1306_128X32_UNIVISION_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 5, /* data=*/ 4, /* reset=*/ 16);

void setup()
{
  Serial.begin(115200);

  WiFiManager wifiManager;  
  wifiManager.autoConnect("AP-NAME", "appa55$");

  delay(1000);

  //Set time
  webUnixTime(client);
  
  Serial.print("Time: ");
  Serial.println(webTime + lastWebUnixTimeMillis/1000);

  u8g2.begin();
  u8g2.setFont(u8g2_font_chikita_tf);

  u8g2.drawStr(0,30,"Setup done");
  
  Serial.println("Setup done");
  
  u8g2.sendBuffer();

  delay(1000);
  
  httpsRequest();
}



int tide_direction = 1;
int x = 80;

void get_data(void) {
  //Query web service to get data.

  x += 5; //increment time

  if (x > 100) {
    x -= 100;
    
    if (tide_direction > 0) {
      //going up, now going down
      tide_direction = -1;
    }else {
    //going down, now going up
      tide_direction = 1;
    }
  }
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
    Serial.println("connecting...");
    // send the HTTP PUT request:
    clientSSL.print("GET /api/datagetter?product=predictions&application=NOS.COOPS.TAC.WL&begin_date=");
    clientSSL.print("20180529");
    clientSSL.print("&end_date=");
    clientSSL.print("20180530");
    clientSSL.println("&datum=MLLW&station=9449679&time_zone=lst_ldt&units=english&interval=hilo&format=json HTTP/1.1");
    clientSSL.println("Host: tidesandcurrents.noaa.gov");
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
    /*
    // Extract values
    Serial.println(F("Response:"));

    Serial.print("t= ");
    Serial.println(root["predictions"][0]["t"].as<char*>());
    Serial.print("v= ");
    Serial.println(root["predictions"][0]["v"].as<char*>());
    Serial.print("type= ");
    Serial.println(root["predictions"][0]["type"].as<char*>());
  */
    // Disconnect  
    delay(10);
    clientSSL.flush();
    clientSSL.stop();

    //TODO: Set tide_direction and x
    uint32_t now = webTime + millis()/1000;
    Serial.print("now: "); Serial.println(now);
    
    for (int i=0; i < sizeof(root["predictions"]); i++) {
      //t is in format: 2018-05-29 04:56
      String t = root["predictions"][i]["t"].as<char*>();
      Serial.print("month substring1="); Serial.println(t.substring(5, 2));
      Serial.print("month substring2="); Serial.println(t.substring(5*2, 2));
      uint32_t prediction = makeTime(t.substring(0, 4).toInt(), t.substring(5, 2).toInt(), t.substring(8, 2).toInt(), t.substring(11, 2).toInt(), t.substring(14, 2).toInt(), 0);

      Serial.print("prediction t= "); Serial.print(t); Serial.print(", ts= "); Serial.print(prediction); 
      Serial.print(", v= "); Serial.println(root["predictions"][i]["v"].as<char*>());
      
      if (prediction <= now) {
        if(root["predictions"][i]["type"].as<char*>() == "H")
          tide_direction = -1; // tide going down
        else
          tide_direction = 1; // tide going up

        Serial.println("match!");
        break;
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
    
  if (tide_direction < 0) {
    //u8g2.print("Tide going down");
    u8g2.drawLine(1, 1, 100, 20);
  } else {
    //u8g2.print("Tide going up");
    u8g2.drawLine(1, 20, 100, 1);
  }

  u8g2.drawLine(x, 1, x, 20);

  u8g2.setCursor(0,30);
  //u8g2.print("low tide 11:03am (-0.20ft), high tide 6:09pm (8.34ft)");  
  u8g2.print("http:");
  u8g2.print(client.connected());

  if (webTime == 0) {
    Serial.println("Get webUnixTime in display_data");
    webUnixTime(client);
  }

  u8g2.print(" T: ");
  u8g2.print(webTime + (millis() - lastWebUnixTimeMillis)/1000);
  u8g2.print(".....");
  
  u8g2.sendBuffer();
}

void loop()
{ 
  loop_count++;
  Serial.print("webTime before: ");
  Serial.print(webTime);
  Serial.print(" lastWebUnixTimeMillis: ");
  Serial.println(lastWebUnixTimeMillis);
  
  if (webTime == 0 || (millis() - lastWebUnixTimeMillis)/1000 > refreshWebTimeInveralSeconds) {
    Serial.println("Get webUnixTime before");
    webUnixTime(client);
    Serial.println("Get webUnixTime after");
  }
  
  if (webTime > 0) {
    Serial.println("before httpsRequest()");
    httpsRequest();
    Serial.println("after httpsRequest()");    
  //get_data();
  }

  display_data();
  
  //u8g2.setCursor(101,7);
  u8g2.setCursor(102,7);
  u8g2.print(loop_count);
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
    client.readBytes(buf, 1);    // discard
    client.readBytes(buf, 3);    // month
    int year = client.parseInt();    // year
    byte hour = client.parseInt();   // hour
    byte minute = client.parseInt(); // minute
    byte second = client.parseInt(); // second

    Serial.print("Time: ");
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
      case 'F': daysInPrevMonths =  31; break; // Feb
      case 'S': daysInPrevMonths = 243; break; // Sep
      case 'O': daysInPrevMonths = 273; break; // Oct
      case 'N': daysInPrevMonths = 304; break; // Nov
      case 'D': daysInPrevMonths = 334; break; // Dec
      default:
        if (buf[0] == 'J' && buf[1] == 'a')
    daysInPrevMonths = 0;   // Jan
        else if (buf[0] == 'A' && buf[1] == 'p')
    daysInPrevMonths = 90;    // Apr
        else switch (buf[2])
         {
         case 'r': daysInPrevMonths =  59; break; // Mar
         case 'y': daysInPrevMonths = 120; break; // May
         case 'n': daysInPrevMonths = 151; break; // Jun
         case 'l': daysInPrevMonths = 181; break; // Jul
         default: // add a default label here to avoid compiler warning
         case 'g': daysInPrevMonths = 212; break; // Aug
         }
      }

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


