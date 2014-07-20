/*

To Do:
Add additional time servers in case one or two fail
Make pin 7 the SS since pin 9 has an LED

 
Uses Moteino R4
Pinout: http://farm4.staticflickr.com/3818/10585364014_d028c66523_o.png
MoteinoHardware files: http://github.com/LowPowerLab/Moteino
Moteino Forum: https://lowpowerlab.com/forum/
Davis Serial Communication Reference Manual: http://www.davisnet.com/support/weather/downloads/software_dllsdk.asp
Ethernet library where you can choose SS pin: http://forum.arduino.cc/index.php?topic=217423.msg1601862#msg1601862 


Rain calculation:
I think the way rain works is every time the the bucket tips, the byte counts up. It stays there at that value until it tips again.  When it reaches 255 it goes back to zero
References: http://madscientistlabs.blogspot.ca/2012/05/here-comes-rain.html
            http://www.wxforum.net/index.php?topic=22082.msg212375#msg212375
            http://www.wxforum.net/index.php?topic=21550.new;topicseen#new - Arduino Uno weather station        
 

Uses of the DavisRFM69 library to sniff the packets from a Davis Instruments
wireless Integrated Sensor Suite (ISS), demostrating compatibility between the RFM69
and the TI CC1020 transmitter used in that hardware. 
See http://madscientistlabs.blogspot.com/2014/02/build-your-own-davis-weather-station_17.html

This is part of the DavisRFM69 library from https://github.com/dekay/DavisRFM69
(C) DeKay 2014 dekaymail@gmail.com
Example released under the MIT License (http://opensource.org/licenses/mit-license.php)


Weather Underground upload info
http://wiki.wunderground.com/index.php/PWS_-_Upload_Protocol
Sample ULR
http://weatherstation.wunderground.com/weatherstation/updateweatherstation.php?ID=KCASANFR5&PASSWORD=XXXXXX&dateutc=2000-01-01+10%3A32%3A35&winddir=230&windspeedmph=12&windgustmph=12&tempf=70&rainin=0&baromin=29.1&dewptf=68.2&humidity=90&weather=&clouds=&softwaretype=vws%20versionxx&action=updateraw
Test PWS: http://www.wunderground.com/personal-weather-station/dashboard?ID=KVTDOVER3

 
 
Pins:
A4 SDA 
A5 SCL
D5 - Red LED
D6 - Green LED
D7 - Slave select - on v2 of PCB
D8 - SS for flash (not used in this project)
D9  PCB Led
D10 Slave select for RFM69
D11-13 - used for SPI

Change log:
 

*/

#define VERSION "v0.1"  // version of this program


#include <DavisRFM69.h>        // http://github.com/dekay/DavisRFM69
#include <Ethernet.h>          // Modified for user selectable SS pin  http://forum.arduino.cc/index.php?topic=217423.msg1601862#msg1601862
#include <EthernetUdp.h>
#include <SPI.h>               // DavisRFM69.h needs this   http://arduino.cc/en/Reference/SPI
#include <Wire.h>
#include "RTClib.h"            // http://github.com/adafruit/RTClib
#include <Adafruit_BMP085_U.h> // http://github.com/adafruit/Adafruit_BMP085_Unified
#include <Adafruit_Sensor.h>   // http://github.com/adafruit/Adafruit_Sensor
#include "Tokens.h"            // Holds Weather Underground password

// Reduce number of bogus compiler warnings
// See: http://forum.arduino.cc/index.php?PHPSESSID=uakeh64e6f5lb3s35aunrgfjq1&topic=102182.msg766625#msg766625
#undef PROGMEM
#define PROGMEM __attribute__(( section(".progmem.data") ))

#if !defined(__time_t_defined)
  typedef unsigned long time_t;
#endif

#define PRINT_DEBUG

// Header bytes from ISS (weather station tx) that determine what measurement is being sent
// Valid values are: 40 50 60 80 90 A0 e0
// See http://github.com/dekay/im-me/blob/master/pocketwx/src/protocol.txt and bit.ly/1kDsXK4   
#define ISS_OUTSIDE_TEMP 0x80
#define ISS_HUMIDITY     0xA0
#define ISS_RAIN         0xE0
#define ISS_SOLAR_RAD    0x60
#define ISS_UV_INDEX     0x40

#define WUNDERGROUND_STATION_ID "KVTDOVER3" // Weather Underground station ID
char SERVER [] = "weatherstation.wunderground.com";  // standard server
//char SERVER[] = "rtupdate.wunderground.com";           // Realtime update server


// Ethernet and time server setup
IPAddress timeServer( 132, 163, 4, 101 );    // ntp1.nl.net NTP server
// IPAddress timeServer (132, 163, 4, 102); // time-b.timefreq.bldrdoc.gov NTP server
// IPAddress timeServer (132, 163, 4, 103);
// IPAddress timeServer (192, 43, 244, 18); // time.nist.gov NTP server
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming & outgoing packets

byte moteinoMac[] = { 0xDE, 0xAD, 0xBE, 0xAA, 0xAB, 0xA4 };
IPAddress moteinoIp( 192, 168, 216, 46 );

EthernetClient client;
EthernetUDP Udp;          // UDP for the time server

// Weather data that's not in LoopPacket in DavidRFM69.cpp
byte rainCounter = 0;   // rain data sent from outside weather station.  1 = 0.01".  Just counts up to 255 then rolls over to zero
byte windgustmph = 0;   // Wind in MPH
float dewpoint = 0.0;   // Dewpoint F

const byte RED_LED = 5;
const byte GRN_LED = 6;
const byte MOTEINO_LED = 8;  // Change to pin 9 in v2
const byte ETH_SS_PIN = 9;  // Ethernet slave select pin, chage to pin 7 in v2

DavisRFM69 radio;
LoopPacket loopData;  // structure to hold most of the weather station data (see DavisRFM69.h). There's a lot of unused variables because this sketch doesn't interface with the weather console
RTC_Millis rtc;  // software clock
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085); // Barometric pressure sensor, uses I2C. http://www.adafruit.com/products/1603

// Function Prototypes
bool getWirelessData();
void processPacket();
bool uploadWeatherData();
void updateRainAccum();
void updateWindGusts();
bool updateBaromoter();
bool updateInsideTemp();
float dewPointFast(float tempF, byte humidity);
float dewPoint(float tempf, byte humidity);
void printFreeRam();
void printStrm();
void blink(byte PIN, int DELAY_MS);
void getUtcTime(char timebuf[]);
bool isNewMinute();
bool isNewDay();
int estOffset();
void printURL();
time_t getNtpTime();
void sendNTPpacket(IPAddress &address);
void softReset();

//=============================================================================
//=============================================================================
void setup() 
{
  Serial.begin(9600);
  
  pinMode(MOTEINO_LED, OUTPUT);
  pinMode(GRN_LED,     OUTPUT);
  pinMode(RED_LED,     OUTPUT);

  // Flash LEDs
  for (int i = 0; i < 6; i++)
  {
    digitalWrite(MOTEINO_LED, !digitalRead(MOTEINO_LED));
    digitalWrite(GRN_LED,     !digitalRead(GRN_LED));
    digitalWrite(RED_LED,     !digitalRead(RED_LED));
    delay(50);
  }
  
  // Setup Moteino radio
//srg  radio.initialize();
//srg  radio.setChannel(0);      // Frequency / Channel is *not* set in the initialization. Do it right after.

  
  memcpy(&loopData, &loopInit, sizeof(loopInit));  // Initialize the loop data array
  
  // Setup Ethernet
  Ethernet.select(ETH_SS_PIN);  // Set slave select pin - requires modified Ethernet.h library
//  Ethernet.begin(moteinoMac, moteinoIp);  // This uses less memory but isn't working with WIZ811MJ; works fine with Ethernet shield
  Ethernet.begin(moteinoMac);  
  Serial.println(Ethernet.localIP());
  Udp.begin(8888);  // local port 8888 to listen for UDP packet
  
  // Get time from NTP server
  time_t t = getNtpTime();
  if ( t != 0 )
  { 
    rtc.begin(DateTime(t));
    DateTime now = rtc.now();
    #ifdef PRINT_DEBUG
      Serial.println(F("Got time from NTP server"));
    #endif
  }
  #ifdef PRINT_DEBUG
  else
  { Serial.println(F("Did not get NTP time in setup")); }
  #endif
  
  // Set up BMP085 pressure and temperature sensor
  if (!bmp.begin())
  { Serial.println(F("Could not find a valid BMP180 sensor")); }

  
  Serial.print(F("Weather Station "));
  Serial.println(VERSION);

  printFreeRam();

}  // end setup()


void loop()
{
  static uint32_t uploadTimer = 0; // timer to send data to weather underground
  DateTime now = rtc.now();
  
//srg  getWirelessData();
  updateRainAccum();
  updateWindGusts();
  updateBaromoter();
  updateInsideTemp();
  
  
  // create dummy data
  loopData.windSpeed = 3;
  windgustmph =  loopData.windSpeed * 2;
  loopData.windDirection = 180;
  loopData.outsideTemperature = 689;
  loopData.outsideHumidity = 69;
  loopData.insideTemperature = 7550;
  loopData.rainRate = 0;
  loopData.dayRain = 3;
  loopData.barometer = 30170;
  dewpoint = dewPointFast((float)loopData.outsideTemperature/10.0, loopData.outsideHumidity);
  
  // Send data to Weather Underground
  if( (long)(millis() - uploadTimer) > 0 && now.year() >= 2014 )
  {
    uploadWeatherData();
    uploadTimer = millis() + 10000; // upload again in 10 seconds
    //  printURL();
  }

  
  // Reboot if millis is close to rollover. RTC_Millis won't work properly if millis rolls over.  Takes about 49 days
  if( millis() > 4294000000UL )
  { softReset(); }
  
  
}  // end loop()


// Read wireless data coming from Davis ISS weather station
bool getWirelessData()
{
  
  static uint32_t lastRxTime = 0;
  static byte hopCount = 0;

  bool gotGoodData = false;
  
  // The check for a zero CRC value indicates a bigger problem that will need
  // fixing, but it needs to stay until the fix is in.
  // TODO Reset the packet statistics at midnight once I get my clock module.
  if ( radio.receiveDone() )
  {
    packetStats.packetsReceived++;
    unsigned int crc = radio.crc16_ccitt(radio.DATA, 6);
    if ((crc == (word(radio.DATA[6], radio.DATA[7]))) && (crc != 0))
    {
      processPacket();
      packetStats.receivedStreak++;
      hopCount = 1;
      blink(GRN_LED, 50);
      #ifdef PRINT_DEBUG
        Serial.println(F("Got good packet"));
      #endif
      gotGoodData = true;
    }
    else
    {
      packetStats.crcErrors++;
      packetStats.receivedStreak = 0;
      blink(GRN_LED, 50);
      #ifdef PRINT_DEBUG
        Serial.println(F("Got bad packet"));
      #endif
    }
    
#ifdef PRINT_DEBUG
    Serial.print("ch: ");
    Serial.println(radio.CHANNEL);
    Serial.print(F("Data: "));
    for (byte i = 0; i < DAVIS_PACKET_LEN; i++)
    {
      Serial.print(radio.DATA[i], HEX);
      Serial.print(F(" "));
    }
    Serial.println();
    Serial.print(F("RSSI: "));
    Serial.println(radio.RSSI);
    Serial.println(F("----------------------------"));
#endif
    
    // Whether CRC is right or not, we count that as reception and hop
    lastRxTime = millis();
    radio.hop();
  } // end if(radio.receiveDone())
  
  // If a packet was not received at the expected time, hop the radio anyway
  // in an attempt to keep up.  Give up after 25 failed attempts.  Keep track
  // of packet stats as we go.  I consider a consecutive string of missed
  // packets to be a single resync.  Thx to Kobuki for this algorithm.
  const int PACKET_INTERVAL = 2555;
  if ( (hopCount > 0) && ((millis() - lastRxTime) > (hopCount * PACKET_INTERVAL + 200)) )
  {
    packetStats.packetsMissed++;
    if (hopCount == 1) packetStats.numResyncs++;
    if (++hopCount > 25) hopCount = 0;
    radio.hop();
  }

  return gotGoodData;
  
} // end getWirelessData()

//=============================================================================
// Read the data from the ISS 
//=============================================================================
void processPacket() {
  // Every packet has wind speed, direction and battery status in it
  loopData.windSpeed = radio.DATA[1];
  
  #ifdef PRINT_DEBUG
    Serial.print("Wind Speed: ");
    Serial.print(loopData.windSpeed);
    Serial.print("  Rx Byte 1: ");
    Serial.println(radio.DATA[1]);
  #endif

  // There is a dead zone on the wind vane. No values are reported between 8
  // and 352 degrees inclusive. These values correspond to received byte
  // values of 1 and 255 respectively
  // See http://www.wxforum.net/index.php?topic=21967.50
  loopData.windDirection = 9 + radio.DATA[2] * 342.0f / 255.0f;

  #ifdef PRINT_DEBUG
    Serial.print(F("Wind Direction: "));
    Serial.print(loopData.windDirection);
    Serial.print(F("  Rx Byte 2: "));
    Serial.println(radio.DATA[2]);
  #endif
  
  // 0 = battery ok, 1 = battery low
  loopData.transmitterBatteryStatus = (radio.DATA[0] & 0x8) >> 3;
  
  #ifdef PRINT_DEBUG
    Serial.print("Battery status: ");
    Serial.println(loopData.transmitterBatteryStatus);
  #endif

  // Now look at each individual packet. Mask off the four low order bits. The highest order bit of the
  // four is set high when the ISS battery is low.  The low order three bits are the station ID.
  switch (radio.DATA[0] & 0xf0) 
  {

  float outHumidity;  // outside humidity, used for rounding to nearest integer
  
  case ISS_OUTSIDE_TEMP:
    loopData.outsideTemperature = (int16_t)(word(radio.DATA[3], radio.DATA[4])) >> 4;
    #ifdef PRINT_DEBUG
      Serial.print(F("Outside Temp: "));
      Serial.print(loopData.outsideTemperature);
      Serial.print(F("  Byte 3: 0x"));
      Serial.print(radio.DATA[3], HEX);
      Serial.print(F("  Byte 4: 0x"));
      Serial.println(radio.DATA[4], HEX);
    #endif
    break;

  case ISS_HUMIDITY:
    outHumidity = (float)( word((radio.DATA[4] >> 4), radio.DATA[3]) ) / 10.0;
    // Round humidity to nearest integer
    loopData.outsideHumidity = (byte) ( outHumidity + 0.5 );
    dewpoint = dewPointFast( (float)loopData.outsideTemperature/10.0, loopData.outsideHumidity);
    #ifdef PRINT_DEBUG
      Serial.print("Outside Humidity: ");
      Serial.print(loopData.outsideHumidity);
      Serial.print(F("  Byte 3: 0x"));
      Serial.print(radio.DATA[3], HEX);
      Serial.print(F("  Byte 4: 0x"));
      Serial.println(radio.DATA[4], HEX);
    #endif
    break;
    
  case ISS_RAIN: 
    rainCounter =  radio.DATA[3];
    #ifdef PRINT_DEBUG
      Serial.print("Rain: ");
      Serial.print(rainCounter);
      Serial.print(F("  Byte 3: "));
      Serial.print(radio.DATA[3]);
      Serial.print(F("  Byte 4: "));
      Serial.println(radio.DATA[4]);
    #endif
    break;

   case ISS_SOLAR_RAD:
   loopData.solarRadiation = 0; // srg - need forumla
    #ifdef PRINT_DEBUG
      Serial.print("Solar Raidiation: ?? ");
      Serial.print(loopData.solarRadiation);
      Serial.print(F("  Byte 3: 0x"));
      Serial.print(radio.DATA[3], HEX);
      Serial.print(F("  Byte 4: 0x"));
      Serial.println(radio.DATA[4], HEX);
    #endif
    break;
     
   case ISS_UV_INDEX:
   loopData.uV = 0; // srg - need forumla
    #ifdef PRINT_DEBUG
      Serial.print("UV Index: ?? ");
      Serial.print(loopData.uV);
      Serial.print(F("  Byte 3: 0x"));
      Serial.print(radio.DATA[3], HEX);
      Serial.print(F("  Byte 4: 0x"));
      Serial.println(radio.DATA[4], HEX);
    #endif
    break;

    default:
    #ifdef PRINT_DEBUG
      Serial.print("Other header: 0x");
      Serial.print(radio.DATA[0] & 0xf0, HEX);
      Serial.print(F("  Byte 3: 0x"));
      Serial.print(radio.DATA[3], HEX);
      Serial.print(F("  Byte 4: 0x"));
      Serial.println(radio.DATA[4], HEX);
    #endif
    break; 
  }
  
} //  end processPacket()


// Upload to Weather Underground
bool uploadWeatherData()
{
   // Get UTC time and format
  char dateutc[25];
  getUtcTime(dateutc);  

  // Send the Data to weather underground
  if (client.connect(SERVER, 80))
  {
    client.print("GET /weatherstation/updateweatherstation.php?ID=");
    client.print(WUNDERGROUND_STATION_ID);
    client.print("&PASSWORD=");
    client.print(WUNDERGROUND_PWD);
    client.print("&dateutc=");
    client.print(dateutc);
    client.print("&winddir=");
    client.print(loopData.windDirection);
    client.print("&windspeedmph=");
    client.print(loopData.windSpeed);
    client.print("&windgustmph=");
    client.print(windgustmph);
    client.print("&tempf=");
    client.print((float)loopData.outsideTemperature / 10.0);
    if ( loopData.insideTemperature > 350 )  // remember temp is in 1/10 degrees
    {
      client.print("&indoortempf=");
      client.print((float)loopData.insideTemperature / 10.0);
    }
    client.print("&rainin=");
    client.print((float)loopData.rainRate / 100.0);  // rain inches over the past hour
    client.print("&dailyrainin=");   // rain inches so far today in local time
    client.print((float)loopData.dayRain / 100.0);  //
    client.print("&baromin=");
    client.print((float)loopData.barometer / 1000.0);
    client.print("&dewptf=");
    client.print(dewpoint);
    client.print("&humidity=");
    client.print(loopData.outsideHumidity);
    client.print("&softwaretype=Arduino%20Moteino%20");
    client.print(VERSION);
    client.print("&action=updateraw");
    client.println();
  }
  else
  {
    #ifdef PRINT_DEBUG
      Serial.println(F("\nWunderground connection failed"));
    #endif
    client.stop();
    delay(500);
    return false;
  }
  
  uint32_t lastRead = millis();
  while (client.connected() && (millis() - lastRead < 1000))  // wait up to one second for server response
  {
    if (client.available())
    {
      char c = client.read();
      #ifdef PRINT_DEBUG
        Serial.print(c);
      #endif
    }
  }
  
  client.stop();
  return true;
  
} // end uploadWeatherData()


// Updates loopData.rainRate - rain in past 60 minutes
//         loopData.dayRain - rain today (local time)
// Everything is in 0.01" units of rain
void updateRainAccum()
{
  static uint8_t rainEachMinute[60]; // array holds incremental rain for each minute of the hour
  static uint8_t prevRainCnt = 0;    // rain count (not incremental) from previous minute
  
  if ( isNewMinute() )
  {
    DateTime now = rtc.now();
    uint8_t newMinute = now.minute();
    
    // Calculate new rain since since the last minute
    int newRain; // incremental new rain since last minute
    if ( rainCounter < prevRainCnt )
    { newRain = (256 - prevRainCnt) + rainCounter; } // counter has rolled over
    else
    { newRain = rainCounter - prevRainCnt; }
    
    // add new rain and remove rain from an hour ago
    loopData.rainRate = loopData.rainRate + newRain - rainEachMinute[newMinute];
    
    rainEachMinute[newMinute] = newRain;  // Update array with latest rain amount
    prevRainCnt = rainCounter;
    
    // Increment daily rain counter
    loopData.dayRain += newRain;
  }
  
  // reset daily rain accumulation
  if ( isNewDay() );
  { loopData.dayRain = 0; }
  
} // end updateRainAccum()


// Update wind gust calculation
// Weather Underground wind variables.  Sketch only uses: winddir, windspeedmph, windgustmph
// winddir - [0-360 instantaneous wind direction]
//    windspeedmph - [mph instantaneous wind speed]
//    windgustmph - [mph current wind gust, using software specific time period]
//    windgustdir - [0-360 using software specific time period]
//    windspdmph_avg2m  - [mph 2 minute average wind speed mph]
//    winddir_avg2m - [0-360 2 minute average wind direction]
//    windgustmph_10m - [mph past 10 minutes wind gust mph ]
//    windgustdir_10m - [0-360 past 10 minutes wind gust direction]
void updateWindGusts()
{
  const uint32_t TWO_MINUTES = 120000;
  static uint32_t windGustTmr2min = 0;  // two minute wind gust timer.
  
  if ( (long)(millis() - windGustTmr2min) > 0 )
  {
    windgustmph = loopData.windSpeed;  // reset wind gust
    windGustTmr2min = millis() + TWO_MINUTES;
  }
  else
  {
    if ( loopData.windSpeed > windgustmph )
    { windgustmph = loopData.windSpeed; }
  }

}  // end updateWindGusts()


// Get pressure reading from baromoter - located inside, not outside on weather station
// Barometric pressure is read once per minute
// Units from the BMP085 are Pa.  Need to convert to thousandths of inches of Hg.
bool updateBaromoter()
{

  sensors_event_t event;
  bmp.getEvent(&event);
  
  if (event.pressure)
  {
    loopData.barometer = event.pressure;
    return true;
  }
  else
  {
    #ifdef PRINT_DEBUT
      Serial.println(F("Barometric sensor error"));
    #endif
    return false;
  }
 
} // end updateBaromoter()


// Inside temperature from BMP180 sensor
bool updateInsideTemp()
{
  float temperature;
  bmp.getTemperature(&temperature);
  if (temperature > 35 && temperature < 110) // Validate indoor temperature range
  {
    loopData.insideTemperature = temperature * 10.0;
    return true;
  }
  else
  { return false; }
  
} // end updateInsideTemp()


// Calculate the dew point, this is supposed to do fast
// Ref: http://playground.arduino.cc/Main/DHT11Lib
float dewPointFast(float tempF, byte humidity)
{
  float celsius = ( tempF - 32.0 ) / 1.8;
  float a = 17.271;
  float b = 237.7;
  float temp = (a * celsius) / (b + celsius) + log( (float)humidity * 0.01);
  float Td = (b * temp) / (a - temp);
  Td = Td * 1.8 + 32.0;  // convert back to Fahrenheit
  return Td;
} // end dewPointFast()

// Another dewpoint calculation
float dewPoint(float tempf, byte humidity)
{
  float A0 = 373.15/(273.15 + tempf);
  float SUM = -7.90298 * (A0-1);
  SUM += 5.02808 * log10(A0);
  SUM += -1.3816e-7 * (pow(10, (11.344*(1-1/A0)))-1) ;
  SUM += 8.1328e-3 * (pow(10,(-3.49149*(A0-1)))-1) ;
  SUM += log10(1013.246);
  float VP = pow(10, SUM-3) * (float) humidity;
  float T = log(VP/0.61078);
  return (241.88 * T) / (17.558-T);
} // end dewPoint()



// Format GMT Time the way weather underground wants
// dateutc - [YYYY-MM-DD HH:MM:SS (mysql format)]
// Time is 23 characters long, 
// Data need to be URL escaped: 2014-01-01 10:32:35 becomes 2014-01-01+10%3A32%3A35
// Every 10 minutes sketch will query NTP time server to update the time
void getUtcTime(char timebuf[])
{
  static uint32_t refreshNTPTimeTimer = 0; // Timer used to determine when to get time from NTP time server
   
  if( (long)(millis() - refreshNTPTimeTimer) > 0 )
  {
    // Update NTP time
    time_t t = getNtpTime();
    if ( t != 0 )
    {
      rtc.adjust(DateTime(t));
      refreshNTPTimeTimer = millis() + 600000;  // Get NTP time again in 10 minutes
    }
    else
    {
      #ifdef PRINT_DEBUG
        Serial.println("NTP Update failed");
     #endif
    }
  }
  // Get UTC time and format for weather underground
  DateTime now = rtc.now();
  sprintf(timebuf, "%d-%02d-%02d+%02d%%3A%02d%%3A%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());

} // end getUtcTime()


// Returns true if there there is a new minute
// Used by rain accumulation array
bool isNewMinute()
{
  DateTime now = rtc.now();
  static uint8_t prevMinute = now.minute();
  if ( prevMinute == now.minute() )
  { return false; }
  else
  { 
    prevMinute = now.minute();
    return true; 
  }
} // end isNewMinute()

// Return true if it's a new day. This is local (EST) time, not UTC
// Used to reset daily rain accumulation
bool isNewDay()
{
  DateTime est = rtc.now();
  
  // crude daylight savings adjustment
  if (est.month() > 3 && est.month() < 11 )
  { DateTime est =  est + (3600UL * -4L);} // summer
  else
  { DateTime est =  est + (3600UL * -5L);} // winter
  
  static uint8_t prevDay = est.day();
  if ( prevDay == est.day())
  { return false; }
  else
  {
    prevDay = est.day();
    return true; 
  }
} // end isNewDay()


// Figure out if daylight savings time or not and return EST offset from GMT
// Summer 4 hours, winter 5 hours
int estOffset()
{
  return -4;
} // end estOffset()



// From http://jeelabs.org/2011/05/22/atmega-memory-use/
void printFreeRam() 
{
  extern int __heap_start, *__brkval;
  int v;
  Serial.print(F("Free mem: "));
  Serial.println((int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval));
} // end printFreeRam()


// Print the packet contents from Davis ISS
void printStrm() 
{
  for (byte i = 0; i < DAVIS_PACKET_LEN; i++) 
  {
    Serial.print("radio.DATA[");
    Serial.print(i);
    Serial.print("] = ");
    Serial.print(radio.DATA[i], HEX);
    Serial.println();
  }
  Serial.println();
} // end printStrm()



time_t getNtpTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  { sendNTPpacket(timeServer); }
  
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500)
  {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE)
    {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer. This is NTP time (seconds since Jan 1 1900):
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;
      return secsSince1900 - seventyYears;
    }
  }  // end while()
  
  return 0; // return 0 if unable to get the time
}  // end getNtpTime()


// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
} // end sendNTPpacket()


// Blink LED
void blink(byte pin, int DELAY_MS)
{
  digitalWrite(pin, HIGH);
  delay(DELAY_MS);
  digitalWrite(pin, LOW);
} // end blink()

// Reboot
void softReset()
{
  asm volatile ("  jmp 0");
}

// srg - so you can see what URL will look like
// You should be able to past it right in browser and update the PWS station
void printURL()
{
   // Get time stamp
  char dateutc[25]; // holds UTC Time
  getUtcTime(dateutc);  
  
  Serial.print("\nhttp://weatherstation.wunderground.com/weatherstation/updateweatherstation.php?ID=");
  Serial.print(WUNDERGROUND_STATION_ID);
  Serial.print("&PASSWORD=");
  Serial.print(WUNDERGROUND_PWD);
  Serial.print("&dateutc=");
  Serial.print(dateutc);
  Serial.print("&winddir=");
  Serial.print(loopData.windDirection);
  Serial.print("&windspeedmph=");
  Serial.print(loopData.windSpeed);
  Serial.print("&windgustmph=");
  Serial.print(windgustmph);
  Serial.print("&tempf=");
  Serial.print((float)loopData.outsideTemperature / 10.0);
  Serial.print("&rainin=");
  Serial.print((float)loopData.rainRate / 100.0);  // rain inches over the past hour
  Serial.print("&dailyrainin=");   // rain inches so far today in local time
  Serial.print((float)loopData.dayRain / 100.0);  //
  Serial.print("&baromin=");
  Serial.print((float)loopData.barometer / 1000.0);
  Serial.print("&dewptf=");
  Serial.print(dewpoint);
  Serial.print("&humidity=");
  Serial.print(loopData.outsideHumidity);
  Serial.print("&softwaretype=Arduino%20Moteino%20v1.00");
  Serial.print("&action=updateraw");
  
  Serial.println();
  
} // end printURL()


