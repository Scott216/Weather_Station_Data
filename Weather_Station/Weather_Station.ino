/*

To Do:
Add additional time servers in case one or two fail



References: http://madscientistlabs.blogspot.ca/2012/05/here-comes-rain.html
            http://www.wxforum.net/index.php?topic=22082.msg212375#msg212375
            http://www.wxforum.net/index.php?topic=21550.new;topicseen#new - Arduino Uno weather station        
 
Test PWS: http://www.wunderground.com/personal-weather-station/dashboard?ID=KVTDOVER3

Note: It can take a while for the radio to start receiving packets, I think it's figuring out the frequency hopping or something
 
Pins:
A4 SDA 
A5 SCL
A0 - Rx Good LED (green)
A1 - Rx Bad LED (red)
D2 - Tx Good LED (green)
D7 - Slave select - on v2 of PCB
D8 - SS for flash (not used in this project)
D9  Moteino PCB Led
D10 Slave select for RFM69
D11-13 - used for SPI

Change log:
07/23/14 v0.12 - fixed isNewDay bug.  Averaged wind data and increased wind gust period to 5 minutes.  
07/24/14 v0.13 - Changed rainRate calculation, looks at 5 minutes instead on an hour, added station ID check

*/

#define VERSION "v0.13"  // version of this program

#include <DavisRFM69.h>        // http://github.com/dekay/DavisRFM69
#include <Ethernet.h>          // Modified for user selectable SS pin and disables interrupts  
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

#define PRINT_DEBUG  // comment out to remove many of the Serial.print statements

// Header bytes from ISS (weather station tx) that determine what measurement is being sent
// Valid hex values are: 40 50 60 80 90 A0 E0
// Don't know what 0x50 and 0x90 are for
// See http://github.com/dekay/im-me/blob/master/pocketwx/src/protocol.txt and bit.ly/1kDsXK4   
#define ISS_OUTSIDE_TEMP 0x80
#define ISS_HUMIDITY     0xA0
#define ISS_RAIN         0xE0
#define ISS_SOLAR_RAD    0x60
#define ISS_UV_INDEX     0x40

#define WUNDERGROUND_STATION_ID "KVTDOVER3" // Weather Underground station ID
char SERVER [] = "weatherstation.wunderground.com";  // standard server
//char SERVER[] = "rtupdate.wunderground.com";       // Realtime update server.  Daily rain doesn't work with this server


// Ethernet and time server setup
IPAddress timeServer( 132, 163, 4, 101 );    // ntp1.nl.net NTP server
// IPAddress timeServer (132, 163, 4, 102); // time-b.timefreq.bldrdoc.gov NTP server
// IPAddress timeServer (132, 163, 4, 103);
// IPAddress timeServer (192, 43, 244, 18); // time.nist.gov NTP server
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming & outgoing packets

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xAA, 0xAB, 0xA4 };
IPAddress ip( 192, 168, 46, 131 );

EthernetClient client;
EthernetUDP Udp;          // UDP for the time server

// Weather data to send to Weather Underground
byte rainCounter = 0;            // rain data sent from outside weather station.  1 = 0.01".  Just counts up to 255 then rolls over to zero
byte windgustmph = 0;            // Wind in MPH
float dewpoint = 0.0;            // Dewpoint F
uint16_t dayRain = 0;            // Accumulated rain for the day in 1/100 inch
uint16_t rainRate = 0;           // Rain rate as number of rain clicks (1/100 inch) per hour (e.g 256 = 2.56 in/hr)
int16_t insideTemperature =  0;  // Inside temperature in tenths of degrees
int16_t outsideTemperature = 0;  // Outside temperature in tenths of degrees
uint16_t barometer = 0;          // Current barometer in inches mercury * 1000
byte outsideHumidity = 0;        // Outside relative humidity in %.
byte windSpeed = 0;              // Wind speed in miles per hour
uint16_t windDirection = 0;      // Wind direction from 1 to 360 degrees (0 = no wind data)

// I/O pins
const byte RX_BAD = A1; 
const byte RX_OK = A0;
const byte TX_OK = 2;
const byte MOTEINO_LED = 8;  // Change to pin 9 in v2
const byte ETH_SS_PIN =  9;  // Ethernet slave select pin, chage to pin 7 in v2

// ISS station ID to be monitored
const byte TRANSMITTER_STATION_ID = 1; 

uint32_t timestampRxGoodPacket = 0; // timestamp of last good packet Moteino received

// Used to track first couple of rain readings so initial rain counter can be set
enum rainReading_t { NO_READING, FIRST_READING, AFTER_FIRST_READING };
rainReading_t initialRainReading = NO_READING;  // Need to know when very first rain counter reading comes so inital calculation is correct  

// flag to indicate when weather data is first received.  Used to prevent zeros from being uploaded to Weather Underground upon startup
bool gotInitialWeatherData = false; 

// Instantiate class objects
DavisRFM69 radio;
RTC_Millis rtc;  // software clock
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085); // Barometric pressure sensor, uses I2C. http://www.adafruit.com/products/1603

// Function Prototypes
bool getWirelessData();
void processPacket();
bool uploadWeatherData();
void updateRainAccum();
void updateWind(byte currentWindSpeedMph, uint16_t wind_direction);
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
  pinMode(RX_OK,       OUTPUT);
  pinMode(RX_BAD,      OUTPUT);
  pinMode(TX_OK,       OUTPUT);

  // Flash all LEDs
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
// Ethernet.begin(mac, ip);  // This uses less memory but isn't working with WIZ811MJ; works fine with Ethernet shield
  Ethernet.begin(mac);  
  Serial.println(Ethernet.localIP());
  Udp.begin(8888);  // local port 8888 to listen for UDP packet for NTP time server
  
  // Get time from NTP server
  time_t t = getNtpTime();
  if ( t != 0 )
  { 
    rtc.begin(DateTime(t));
//    DateTime now = rtc.now();
  }
  #ifdef PRINT_DEBUG
  else
  { Serial.println(F("Did not get NTP time")); }
  #endif
  
  // Set up BMP085 pressure and temperature sensor
  if (!bmp.begin())
  { 
    #ifdef PRINT_DEBUG
      Serial.println(F("Could not find BMP180")); 
    #endif
  }

  // Setup Moteino radio
  radio.initialize();
  radio.setChannel(0);      // Frequency / Channel is *not* set in the initialization. Do it right after.

  Serial.print("Weather Station ");
  Serial.println(VERSION);

  #ifdef PRINT_DEBUG
    printFreeRam();
    Serial.println();
  #endif
  
}  // end setup()


void loop()
{
  static uint32_t uploadTimer = 0; // timer to send data to weather underground
  DateTime now = rtc.now();
  
  getWirelessData();
  
/*  // dummy data for tesing
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
  barometer = 29984; 
*/

  
  // Send data to Weather Underground PWS
  bool isTimeToUpload = (long)(millis() - uploadTimer) > 0;
  bool haveFreshWeatherData = ( (long)(millis() - timestampRxGoodPacket) < 60000 );  // Received good packets within the last 60 seconds 
  bool isNptTimeOk = (now.year() >= 2014);
  if( isTimeToUpload && isNptTimeOk && haveFreshWeatherData && gotInitialWeatherData )
  {
    // Get date that's not from outside weather station
    dewpoint = dewPoint( (float)outsideTemperature/10.0, outsideHumidity);
    // updateBaromoter();  //srg - check conversion 
    // updateInsideTemp();

    uploadWeatherData();
    uploadTimer = millis() + 20000; // upload again in 10 seconds
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

  // station ID - the low order three bits are the station ID.  Station ID 1 is 0 in this data
  byte stationId = (radio.DATA[0] & 0x07) + 1;
  if ( stationId != TRANSMITTER_STATION_ID )
  { return; }  // exit function if this isn't the station ID program is monitoring

  // Every packet has wind speed, wind direction and battery status in it
  byte windMPH = radio.DATA[1];
  #ifdef PRINT_DEBUG
    Serial.print(F("Wind Speed: "));
    Serial.println(windMPH);
  #endif

  // There is a dead zone on the wind vane. No values are reported between 8
  // and 352 degrees inclusive. These values correspond to received byte
  // values of 1 and 255 respectively
  // See http://www.wxforum.net/index.php?topic=21967.50
  uint16_t windDir = 9 + radio.DATA[2] * 342.0f / 255.0f;
  #ifdef PRINT_DEBUG
    Serial.print(F("Wind Direction: "));
    Serial.println(windDir);
  #endif

  // update variables tracking wind. 
  updateWind(windMPH, windDir);
  
  // 0 = battery ok, 1 = battery low.  Not used by anything in program
  byte transmitterBatteryStatus = (radio.DATA[0] & 0x8) >> 3; 
  
  // Mask off the four low order bits to see what type of data is being sent
  switch (radio.DATA[0] & 0xf0) 
  {

  case ISS_OUTSIDE_TEMP:
    outsideTemperature = (int16_t)(word(radio.DATA[3], radio.DATA[4])) >> 4;
    gotTempData = true;  // one-time flag when data first arrives.  Used to track when all the data has arrived and can be sent to PWS  
    #ifdef PRINT_DEBUG
      Serial.print(F("Outside Temp: "));
      Serial.println(outsideTemperature);
    #endif
    break;

  case ISS_HUMIDITY:
    // Round humidity to nearest integer
    outsideHumidity = (byte) ( (float)( word((radio.DATA[4] >> 4), radio.DATA[3]) ) / 10.0 + 0.5 );
    gotHumidityData = true;  // one-time flag when data first arrives
    #ifdef PRINT_DEBUG
      Serial.print(F("Humidity: "));
      Serial.println(outsideHumidity);
    #endif
    break;
    
  case ISS_RAIN: 
    rainCounter =  radio.DATA[3];
    if ( initialRainReading == NO_READING )
    { initialRainReading = FIRST_READING; } // got first rain reading
    else if ( initialRainReading == FIRST_READING )
    { initialRainReading = AFTER_FIRST_READING; } // already had first reading, now it's everything after the first time
    gotRainData = true;   // one-time flag when data first arrives
    updateRainAccum();
    #ifdef PRINT_DEBUG
      Serial.print(F("Rain Cnt: "));
      Serial.println(rainCounter);
    #endif
    break;

  case ISS_SOLAR_RAD:
    #ifdef PRINT_DEBUG
      Serial.print(F("Solar: "));
      printStrm();
    #endif
    break;
     
  case ISS_UV_INDEX:
    #ifdef PRINT_DEBUG
      Serial.print(F("UV Index: "));
      printStrm();
    #endif
    break;

  default:
    #ifdef PRINT_DEBUG
      Serial.print(F("Other header: "));
      printStrm();
    #endif
    break; 
  }
  
  // See if all weather data has been received
  // If so, program can start uploading to Weather Underground PWS
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
    if ( outsideTemperature > -400 && outsideTemperature < 1300 )  // temp must be between -40 F and 130 F
    {
      client.print("&tempf=");
      client.print((float)outsideTemperature / 10.0);
    }
    if ( insideTemperature > 350 )  // Inside temp must be > 35 F
    {
      client.print("&indoortempf=");
      client.print((float)insideTemperature / 10.0);
    }
    client.print("&rainin=");
    client.print((float)rainRate / 100.0);  // rain inches over the past hour
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
    client.print("&softwaretype=Arduino%20Moteino");
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


// Once a minuts calculates rain rain in inches per hour
// by looking at the rain for the last 5 minutes, and multiply by 12
// Also track rain accumulation for the day.  Reset at midnight (local time, not UDT time)
// Everything is in 0.01" units of rain
void updateRainAccum()
{
  // For the first time program receives the rain counter value, we need 
  // to set the prevRainCnt to match so we don't count the starting point as rain that came in on this minute 
  static byte prevRainCounter = 0;    // rain count (not incremental) from previous minute
  
  // If program is bootingup, set previous rain counter to current counter so it's not counting rain that not real
  if ( initialRainReading == FIRST_READING )
  { prevRainCounter = rainCounter; }
  
  static byte rainEachMinute[5];  // array holds incremental rain for a 5 minute period
  static byte minuteIndex = 0;    // index for rainEachMinute
  if ( isNewMinute() )
  {
    // Calculate new rain since since the last minute
    byte newRain = 0; // incremental new rain since last minute
    if ( rainCounter < prevRainCounter )
    { newRain = (256 - prevRainCounter) + rainCounter; } // variable has rolled over
    else
    { newRain = rainCounter - prevRainCounter; }
    
    rainEachMinute[minuteIndex++] = newRain;
    if ( minuteIndex == 5 )
    { minuteIndex = 0; }
    
    // calculate hourly rain rate
    uint16_t sumRain = 0;
    for (byte i = 0; i < 5; i++)
    { sumRain += rainEachMinute[i]; }
    rainRate = sumRain * 12;
    
    prevRainCounter = rainCounter;
    
    // Increment daily rain counter
    dayRain += newRain;
  }
  
  // reset daily rain accumulation
  if ( isNewDay() )
  { dayRain = 0; }
  
} // end updateRainAccum()


// Update wind data
// Below are all the Weather Underground wind variables.  This program only uses: winddir, windspeedmph, windgustmph
// windgustmph is over 5 minutes, then resets to current wind speed 
// winddir and windspeedmph are averages of the last 10 readings
// *  winddir - [0-360 instantaneous wind direction]
// *  windspeedmph - [mph instantaneous wind speed]
// *  windgustmph - [mph current wind gust, using software specific time period]
//    windgustdir - [0-360 using software specific time period]
//    windspdmph_avg2m  - [mph 2 minute average wind speed mph]
//    winddir_avg2m - [0-360 2 minute average wind direction]
//    windgustmph_10m - [mph past 10 minutes wind gust mph ]
//    windgustdir_10m - [0-360 past 10 minutes wind gust direction]
void updateWind( byte currentWindSpeedMph, uint16_t windDir )
{
  const uint32_t FIVE_MINUTES = 600000;
  static uint32_t timer_Wind_Gust = 0;  // 5 minute wind gust timer.
  
  if ( (long)(millis() - timer_Wind_Gust) > 0 )
  {
    windgustmph = currentWindSpeedMph;  // reset wind gust
    timer_Wind_Gust = millis() + FIVE_MINUTES;
  }
  else
  {
    if ( currentWindSpeedMph > windgustmph )
    { windgustmph = currentWindSpeedMph; }
  }

  // For for wind speed and direction calculation, take the average of the last 10 radings
  const byte NUM_WIND_READINGS = 10;
  static byte   windSpeedData[NUM_WIND_READINGS];
  static uint16_t windDirData[NUM_WIND_READINGS];
  static byte index = 0;
  
  windDirData[index] = windDir;
  windSpeedData[index++] = currentWindSpeedMph;
  if ( index >= NUM_WIND_READINGS )
  { index = 0; }
  
  float sumWind =      0.0;
  float sumDirection = 0.0;
  for ( byte i = 0; i < NUM_WIND_READINGS; i++ )
  { 
    sumWind +=      windSpeedData[i]; 
    sumDirection += windDirData[i];
  }
  windSpeed =     ( sumWind      / (float)NUM_WIND_READINGS ) + 0.5;  // 0.5 is for rounding
  windDirection = ( sumDirection / (float)NUM_WIND_READINGS ) + 0.5;
  
}  // end updateWind()


// Get pressure reading from baromoter - located inside, not outside on weather station
// Barometric pressure is read once per minute
// Units from the BMP180 are Pa.  Need to convert to thousandths of inches of Hg.
bool updateBaromoter()
{
  sensors_event_t event;
  bmp.getEvent(&event);
  
  if (event.pressure)
  {
    barometer = (float) event.pressure * 29.53 ;  // convert hPa to inches of mercury * 1000
    return true;
  }
  else
  { return false; }
 
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
        Serial.println(F("NTP Update failed"));
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
  { est = est + (3600UL * -4L);} // summer
  else
  { est = est + (3600UL * -5L);} // winter

  static uint8_t prevDay = est.day();
  if ( prevDay == est.day() )
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

// print whole data packet
void printStrm() 
{
  for (byte i = 0; i < DAVIS_PACKET_LEN; i++) 
  {
    Serial.print(radio.DATA[i], HEX);
    Serial.print(F(" "));
  }
  Serial.println();
} // end printStrmIO


// prints same data that sent to the wunderground
void printURL()
{
  #ifdef PRINT_DEBUG
     // Get time stamp
    char dateutc[25]; // holds UTC Time
    getUtcTime(dateutc);  
  
//    Serial.print(F("GET /weatherstation/updateweatherstation.php?ID="));
//    Serial.print(WUNDERGROUND_STATION_ID);
//    Serial.print(F("&PASSWORD="));
//    Serial.print(WUNDERGROUND_PWD);
    Serial.print(F("&dateutc="));
    Serial.print(dateutc);
    Serial.print(F("&winddir="));
    Serial.print(windDirection);
    Serial.print(F("&windspeedmph="));
    Serial.print(windSpeed);
    Serial.print(F("&windgustmph="));
    Serial.print(windgustmph);
    if ( outsideTemperature > -400 && outsideTemperature < 1300 )  // temp must be between -40 F and 130 F
    {
      Serial.print(F("&tempf="));
      Serial.print((float)outsideTemperature / 10.0);
    }
    if ( insideTemperature > 350 )  // remember temp is in 1/10 degrees
    {
      Serial.print(F("&indoortempf="));
      Serial.print((float)insideTemperature / 10.0);
    }
    Serial.print(F("&rainin="));
    Serial.print((float)rainRate / 100.0);  // rain inches over the past hour
    Serial.print(F("&dailyrainin="));   // rain inches so far today in local time
    Serial.print((float)dayRain / 100.0);  //
    if ( barometer > 20000L && barometer < 40000L )
    {
      Serial.print(F("&baromin="));
      Serial.print((float)barometer / 1000.0);
    }
    Serial.print(F("&dewptf="));
    Serial.print(dewpoint);
    Serial.print(F("&humidity="));
    Serial.print(outsideHumidity);
//    Serial.print(F("&softwaretype=Arduino%20Moteino%20"));
//    Serial.print(VERSION);
//    Serial.print(F("&action=updateraw"));
    Serial.println();
    Serial.println();
  #endif
} // end printURL()


