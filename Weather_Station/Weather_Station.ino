/*

To Do:
Add additional time servers in case one or two fail


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


Sketch was locking up because of intrrupts the radio was executing during an Ethernet connection. 
Fixed it by modifying w5100.cpp.  Turned off intrupts in setSS() and turned back on in restSS()
Found that solution here: http://harizanov.com/2012/04/rfm12b-and-arduino-ethernet-with-wiznet5100-chip/

 
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

#define VERSION "v0.11"  // version of this program


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
IPAddress moteinoIp( 192, 168, 46, 131 );

EthernetClient client;
EthernetUDP Udp;          // UDP for the time server

// Weather data that's not in LoopPacket in DavidRFM69.cpp
byte rainCounter = 0;           // rain data sent from outside weather station.  1 = 0.01".  Just counts up to 255 then rolls over to zero
byte windgustmph = 0;           // Wind in MPH
float dewpoint = 0.0;           // Dewpoint F
uint16_t dayRain = 0;           // Accumulated rain for the day
uint16_t rainRate = 0;          // Rain rate as number of rain clicks per hour (e.g 256 = 2.56 in/hr)
int16_t insideTemperature = 0;  // Inside temperature in tenths of degrees
int16_t outsideTemperature;     // Outside temperature in tenths of degrees
uint16_t barometer = 0;         // Current barometer in Hg / 1000
byte outsideHumidity;           // Outside relative humidity in %.
byte windSpeed;                 // Wind speed in miles per hour
uint16_t windDirection;         // Wind direction from 1 to 360 degrees (0 = no wind data)
byte transmitterBatteryStatus;  // Transmitter battery status, 0=ok, 1=low
const byte RX_BAD = 4;  // srg check against schematic
const byte RX_OK = A2;
const byte TX_OK = A1;
const byte MOTEINO_LED = 8;  // Change to pin 9 in v2
const byte ETH_SS_PIN =  9;  // Ethernet slave select pin, chage to pin 7 in v2

uint32_t timestampRxGoodPacket = 0; // timestamp of last good packet Moteino received
enum rainReading_t { NO_READING, FIRST_READING, AFTER_FIRST_READING };
rainReading_t initialRainReading = NO_READING;  // Need to know when very first rain counter reading comes so inital calculation is correct  
bool gotInitialWeatherData = false; // flag to indicate when weather data is first received.  Used to prevent zeros from being uploaded to Weather Underground upon startup


DavisRFM69 radio;
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
  pinMode(RX_OK,       OUTPUT);
  pinMode(RX_BAD,      OUTPUT);
  pinMode(TX_OK,       OUTPUT);

  // Flash LEDs
  for (int i = 0; i < 6; i++)
  {
    digitalWrite(MOTEINO_LED, !digitalRead(MOTEINO_LED));
    digitalWrite(RX_OK,       !digitalRead(RX_OK));
    digitalWrite(RX_BAD,      !digitalRead(RX_BAD));
    digitalWrite(TX_OK,       !digitalRead(TX_OK));
    delay(50);
  }
      
  // Setup Ethernet
  Ethernet.select(ETH_SS_PIN);  // Set slave select pin - requires modified Ethernet.h library
// Ethernet.begin(moteinoMac, moteinoIp);  // This uses less memory but isn't working with WIZ811MJ; works fine with Ethernet shield
  Ethernet.begin(moteinoMac);  
  Serial.println(Ethernet.localIP());
  Udp.begin(8888);  // local port 8888 to listen for UDP packet
  
  // Get time from NTP server
  time_t t = getNtpTime();
  if ( t != 0 )
  { 
    rtc.begin(DateTime(t));
    DateTime now = rtc.now();
  }
  #ifdef PRINT_DEBUG
  else
  { Serial.println(F("Did not get NTP time")); }
  #endif
  
  // Set up BMP085 pressure and temperature sensor
  if (!bmp.begin())
  { Serial.println(F("Could not find BMP180")); }

  // Setup Moteino radio
  radio.initialize();
  radio.setChannel(0);      // Frequency / Channel is *not* set in the initialization. Do it right after.

  Serial.print(F("Weather Station "));
  Serial.println(VERSION);

  printFreeRam();

}  // end setup()


void loop()
{
  static uint32_t uploadTimer = 0; // timer to send data to weather underground
  DateTime now = rtc.now();
  
  getWirelessData();
  updateRainAccum();
  updateWindGusts();
  
/*
  static byte windDir = 0;
  windDir++;
  // create dummy data
  windSpeed = (float) windDir / 75.0;
  windgustmph =  windSpeed * 2;
  windDirection = windDir;
  outsideTemperature = 801;
  outsideHumidity = 59;
  insideTemperature = 7550;
  rainRate = 30;
  dayRain = 0;
*/

  barometer = 30000;  //srg test data
  
  // Send data to Weather Underground
  bool isTimeToUpload = (long)(millis() - uploadTimer) > 0;
  bool haveFreshWeatherData = ( (long)(millis() - timestampRxGoodPacket) < 60000 );  // Received good packets within the last 60 seconds 
  bool isNptTimeOk = (now.year() >= 2014);
  if( isTimeToUpload && isNptTimeOk && haveFreshWeatherData && gotInitialWeatherData )
  {
    // Get date that's not from outside weather station
    dewpoint = dewPoint( (float)outsideTemperature/10.0, outsideHumidity);
    // updateBaromoter();
    // updateInsideTemp();

    uploadWeatherData();
    uploadTimer = millis() + 10000; // upload again in 10 seconds
    printURL();
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
      blink(RX_OK, 50);
      gotGoodData = true;
      timestampRxGoodPacket = millis(); 
    }
    else
    {
      packetStats.crcErrors++;
      packetStats.receivedStreak = 0;
      blink(RX_BAD, 50);
    }
    
    #ifdef PRINT_DEBUG
//      Serial.print(F("ch: "));
//      Serial.println(radio.CHANNEL);
//      Serial.print(F("Data: "));
//      for (byte i = 0; i < DAVIS_PACKET_LEN; i++)
//      {
//        Serial.print(radio.DATA[i], HEX);
//        Serial.print(F(" "));
//      }
//      Serial.println();
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
  const uint16_t PACKET_INTERVAL = 2555;
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
// Every packet contains wind speed, wind direction and battery status
// Packets take turns reporting other data
//=============================================================================
void processPacket() 
{

  // Flags are set true as each variable comes in for the first time
  static bool gotTempData = false;
  static bool gotHumidityData = false; 
  static bool gotRainData = false; 

  // Every packet has wind speed, direction and battery status in it
  windSpeed = radio.DATA[1];
  #ifdef PRINT_DEBUG
    Serial.print("Wind Speed: ");
    Serial.println(windSpeed);
  #endif

  // There is a dead zone on the wind vane. No values are reported between 8
  // and 352 degrees inclusive. These values correspond to received byte
  // values of 1 and 255 respectively
  // See http://www.wxforum.net/index.php?topic=21967.50
  windDirection = 9 + radio.DATA[2] * 342.0f / 255.0f;
  #ifdef PRINT_DEBUG
    Serial.print(F("Wind Direction: "));
    Serial.println(windDirection);
  #endif
  
  // 0 = battery ok, 1 = battery low
  transmitterBatteryStatus = (radio.DATA[0] & 0x8) >> 3;
  
  // Now look at each individual packet. Mask off the four low order bits. The highest order bit of the
  // four is set high when the ISS battery is low.  The low order three bits are the station ID.
  switch (radio.DATA[0] & 0xf0) 
  {

  case ISS_OUTSIDE_TEMP:
    outsideTemperature = (int16_t)(word(radio.DATA[3], radio.DATA[4])) >> 4;
    gotTempData = true; 
    #ifdef PRINT_DEBUG
      Serial.print(F("Outside Temp: "));
      Serial.println(outsideTemperature);
    #endif
    break;

  case ISS_HUMIDITY:
    // Round humidity to nearest integer
    outsideHumidity = (byte) ( (float)( word((radio.DATA[4] >> 4), radio.DATA[3]) ) / 10.0 + 0.5 );
    gotHumidityData = true;
    #ifdef PRINT_DEBUG
      Serial.print("Humidity: ");
      Serial.println(outsideHumidity);
    #endif
    break;
    
  case ISS_RAIN: 
    rainCounter =  radio.DATA[3];
    if ( initialRainReading == NO_READING )
    { initialRainReading = FIRST_READING; } // got first rain reading
    else if ( initialRainReading == FIRST_READING )
    { initialRainReading = AFTER_FIRST_READING; } // already had first reading, now it's everything after the first time
    gotRainData = true; 
    #ifdef PRINT_DEBUG
      Serial.print("Rain Cnt: ");
      Serial.println(rainCounter);
    #endif
    break;

   case ISS_SOLAR_RAD:
   // future  use
    break;
     
   case ISS_UV_INDEX:
     // future use
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
  
  // See if all weather data has been received
  if ( gotTempData && gotHumidityData && gotRainData )
  { gotInitialWeatherData = true; }
  
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
    client.print(windDirection);
    client.print("&windspeedmph=");
    client.print(windSpeed);
    client.print("&windgustmph=");
    client.print(windgustmph);
    if ( outsideTemperature < 1300 && outsideTemperature > -400 )  // temp must be between -40 F and 130 F
    {
      client.print("&tempf=");
      client.print((float)outsideTemperature / 10.0);
    }
    if ( insideTemperature > 350 )  // Inside temp must be > 35 F
    {
      client.print("&indoortempf=");
      client.print((float)insideTemperature / 10.0);
    }
    if ( rainRate < 3500 )  // rain must be less then 3.5 inch per hour
    {
      client.print("&rainin=");
      client.print((float)rainRate / 100.0);  // rain inches over the past hour
    }
    client.print("&dailyrainin=");   // rain inches so far today in local time
    client.print((float)dayRain / 100.0);  //
    if ( barometer > 20000L && barometer < 40000L )
    {
      client.print("&baromin=");
      client.print((float)barometer / 1000.0);
    }
    client.print("&dewptf=");
    client.print(dewpoint);
    if (outsideHumidity <= 100 )
    { 
      client.print("&humidity=");
      client.print(outsideHumidity);
    }
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
  uint32_t connectLoopCounter = 0;  // used to timeout ethernet connection 

  while (client.connected() && (millis() - lastRead < 1000))  // wait up to one second for server response
  {
    while (client.available()) 
    {
      char c = client.read();
      connectLoopCounter = 0;  // reset loop counter
      #ifdef PRINT_DEBUG
        Serial.print(c);
      #endif
    }  
    
    connectLoopCounter++;
    // if more than 10000 loops since the last packet, then timeout
    if(connectLoopCounter > 10000L)
    {
      // then close the connection from this end.
      #ifdef PRINT_DEBUG
        Serial.println(F("\nTimeout"));
      #endif
      client.stop();
    }    
  }  // end while (client.connected() )
  
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
  
  // For the first time program receives the rain counter value, we need 
  // to set the prevRainCnt to match so we don't count the starting point as rain that came in on this minute 
  if ( initialRainReading == NO_READING )
  { return; }     // no reading yet, exit the function
  else if  ( initialRainReading == FIRST_READING )
  { prevRainCnt = rainCounter; }
  
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
    rainRate = rainRate + newRain - rainEachMinute[newMinute];
    
    rainEachMinute[newMinute] = newRain;  // Update array with latest rain amount
    prevRainCnt = rainCounter;
    
    // Increment daily rain counter
    dayRain += newRain;
  }
  
  // reset daily rain accumulation
  if ( isNewDay() );
  { dayRain = 0; }
  
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
    windgustmph = windSpeed;  // reset wind gust
    windGustTmr2min = millis() + TWO_MINUTES;
  }
  else
  {
    if ( windSpeed > windgustmph )
    { windgustmph = windSpeed; }
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
    barometer = event.pressure;
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
    insideTemperature = temperature * 10.0;
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

// prints same data that sent to the wunderground
void printURL()
{
   // Get time stamp
  char dateutc[25]; // holds UTC Time
  getUtcTime(dateutc);  
  
//    Serial.print(F("GET /weatherstation/updateweatherstation.php?ID="));
//    Serial.print(WUNDERGROUND_STATION_ID);
//    Serial.print("&PASSWORD=");
//    Serial.print(WUNDERGROUND_PWD);
    Serial.print("&dateutc=");
    Serial.print(dateutc);
    Serial.print("&winddir=");
    Serial.print(windDirection);
    Serial.print("&windspeedmph=");
    Serial.print(windSpeed);
    Serial.print("&windgustmph=");
    Serial.print(windgustmph);
    Serial.print("&tempf=");
    Serial.print((float)outsideTemperature / 10.0);
    if ( insideTemperature > 350 )  // remember temp is in 1/10 degrees
    {
      Serial.print("&indoortempf=");
      Serial.print((float)insideTemperature / 10.0);
    }
    Serial.print("&rainin=");
    Serial.print((float)rainRate / 100.0);  // rain inches over the past hour
    Serial.print("&dailyrainin=");   // rain inches so far today in local time
    Serial.print((float)dayRain / 100.0);  //
    Serial.print("&baromin=");
    Serial.print((float)barometer / 1000.0);
    Serial.print("&dewptf=");
    Serial.print(dewpoint);
    Serial.print("&humidity=");
    Serial.print(outsideHumidity);
//    Serial.print(F("&softwaretype=Arduino%20Moteino%20"));
//    Serial.print(VERSION);
//    Serial.print(F("&action=updateraw"));
    Serial.println();
  
  Serial.println();
  
} // end printURL()


