/*
Upload sketch: Select Uno as a board.  Need FTDI cable

See Github repository for more information: http://git.io/Ds7oxw
Test weather station http://www.wunderground.com/personal-weather-station/dashboard?ID=KVTDOVER3

To Do:
Automatically switch to alternate time server if main one deosn't work

Note: It can take a while for the radio to start receiving packets, I think it's figuring out the frequency hopping or something
 
I/O:
A4,A5 - I2C communication with MPB180 sensor
A0 - Rx Good LED (green) - good packet received by Moteino
A1 - Rx Bad LED (red) - bad packet received by Moteino
D3 - Tx Good LED (green) - good upload to weather underground
D7 - Slave select for Ethernet module
D8 - Slave Select for Flash chip if  you got the Flash chip option on your Moteino
D9  LED mounted on the Moteino PCB 
D2, D10-13 - used by Moteino transciever

Change log:
07/23/14 v0.12 - fixed isNewDay bug.  Averaged wind data and increased wind gust period to 5 minutes.  
07/24/14 v0.13 - Changed rainRate calculation, looks at 5 minutes instead on an hour, added station ID check
07/25/14 v0.14 - Enabled BPM180 for pressure and inside temp
07/28/14 v0.15 - Moved Tx Ok LED from D2 to D3.  D2 is used by transciever
07/29/14 v0.16 - Formatting changes,  removed printURL(), removed estOffset()
07/30/14 v0.17 - Added comments to help other users
08/06/14 v0.18 - Changed rain calculation from 5 minute buckets to 15 minute buckets
08/07/14 v0.19 - adjusted pressure for sea level, see http://bit.ly/1qYzlE6
08/22/14 v0.20 - Changed rain accumulation to assume ISS rain counter rolls over at 127, not 255
08/26/14 v0.21 - New rain rate calculation (testing)
08/27/14 v0.22 - Decoded packets for rain rate seconds, UV and Solar radiation.  Also changed ISS_... constants to only use MSB/
09/12/14 v0.23 - Changed rain calculation to calculate rate based on seconds between bucket tips
                 Added g_ prefix to global variables.  Removed averaging of current wind speed. Added smoothing algorithm for wind direction.
09/12/14 v0.24   Removed last good packet timer.  Enabled rain rate calculation based on seconds between bucket tips
                 Moved packetBuffer & NTP_PACKET_SIZE from global variables to local.  These are passed as parameters to sendNTPpacket()
                 Changed upload time from 20 to 60 seconds.  Added pinMode() for the SS Pin, use static ip instead of dynamic

*/

#define VERSION "v0.24" // version of this program
#define PRINT_DEBUG     // comment out to remove many of the Serial.print() statements

#include <DavisRFM69.h>        // http://github.com/dekay/DavisRFM69
#include <Ethernet.h>          // Modified for user selectable SS pin and disables interrupts  
#include <EthernetUdp.h>
#include <SPI.h>               // DavisRFM69.h needs this   http://arduino.cc/en/Reference/SPI
#include <Wire.h>
#include "RTClib.h"            // http://github.com/adafruit/RTClib
#include <Adafruit_BMP085_U.h> // http://github.com/adafruit/Adafruit_BMP085_Unified
#include <Adafruit_Sensor.h>   // http://github.com/adafruit/Adafruit_Sensor
#include "Tokens.h"            // Holds Weather Underground password
// #define WUNDERGROUND_PWD "your password"  // uncomment this line and remove #include "Tokens.h" above
#define WUNDERGROUND_STATION_ID "KVTDOVER3" // Weather Underground station ID
const float ALTITUDE = 603;  // Altitude of weather station (in meters).  Used for sea level pressure calculation, see http://bit.ly/1qYzlE6
const byte TRANSMITTER_STATION_ID = 3; // ISS station ID to be monitored

// Reduce number of bogus compiler warnings, see http://bit.ly/1pU6XMe
#undef PROGMEM
#define PROGMEM __attribute__(( section(".progmem.data") ))

#if !defined(__time_t_defined)
  typedef unsigned long time_t;
#endif


// Header bytes from ISS (weather station tx) that determine what measurement is being sent
// Valid hex values are: 40 50 60 80 90 A0 E0  - really only care about MSB, the LSB is staion ID (where 0 is station #1)
// Don't know 0x90 is
// See http://github.com/dekay/im-me/blob/master/pocketwx/src/protocol.txt and bit.ly/1kDsXK4
//     http://www.wxforum.net/index.php?topic=10739.msg190549#msg190549
//     http://www.wxforum.net/index.php?topic=18489.msg178506#msg178506
#define ISS_OUTSIDE_TEMP 0x8
#define ISS_HUMIDITY     0xA
#define ISS_RAIN         0xE
#define ISS_RAIN_SECONDS 0x5  // SRG - needs to be confirmed
#define ISS_SOLAR_RAD    0x6
#define ISS_UV_INDEX     0x4


char g_server [] = "weatherstation.wunderground.com";  // standard server
//char SERVER[] = "rtupdate.wunderground.com";       // Realtime update server.  Daily rain doesn't work with this server


// Ethernet and time server setup
IPAddress timeServer( 132, 163, 4, 101 );   // ntp1.nl.net NTP server
// IPAddress timeServer (132, 163, 4, 102); // time-b.timefreq.bldrdoc.gov NTP server
// IPAddress timeServer (132, 163, 4, 103);
// IPAddress timeServer (192, 43, 244, 18); // time.nist.gov NTP server

byte g_mac[] = { 0xDE, 0xAD, 0xBD, 0xAA, 0xAB, 0xA4 };
byte g_ip[] = { 192, 168, 46, 85 };   

EthernetClient client;
EthernetUDP Udp;          // UDP for the time server

// Weather data to send to Weather Underground
byte     g_rainCounter =        0;  // rain data sent from outside weather station.  1 = 0.01".  Just counts up to 255 then rolls over to zero
byte     g_windgustmph =        0;  // Wind in MPH
float    g_dewpoint =           0.0;  // Dewpoint F
uint16_t g_dayRain =            0;  // Accumulated rain for the day in 1/100 inch
uint16_t g_rainRate =           0;  // Rain rate as number of rain clicks (1/100 inch) per hour (e.g 256 = 2.56 in/hr)
float    g_insideTemperature =  0;  // Inside temperature in tenths of degrees
int16_t  g_outsideTemperature = 0;  // Outside temperature in tenths of degrees
uint16_t g_barometer =          0;  // Current barometer in inches mercury * 1000
byte     g_outsideHumidity =    0;  // Outside relative humidity in %.
byte     g_windSpeed =          0;  // Wind speed in miles per hour
uint16_t g_windDirection =      0;  // Wind direction from 1 to 360 degrees (0 = no wind data)
bool     g_gotInitialWeatherData = false;  // flag to indicate when weather data is first received.  Used to prevent zeros from being uploaded to Weather Underground upon startup

// I/O pins
const byte RX_OK =      A0;  // LED flashes green every time Moteino receives a good packet
const byte RX_BAD =     A1;  // LED flashes red every time Moteinoo receives a bad packet
const byte TX_OK =       3;  // LED flashed green when data is sucessfully uploaed to Weather Underground
const byte MOTEINO_LED = 9;  // PCB LED on Moteino, not used to indicate anything after startup
const byte ETH_SS_PIN =  7;  // Slave select for Etheret module

// Used to track first couple of rain readings so initial rain counter can be set
enum rainReading_t { NO_READING, FIRST_READING, AFTER_FIRST_READING };
rainReading_t g_initialRainReading = NO_READING;  // Need to know when very first rain counter reading comes so inital calculation is correct  

// Instantiate class objects
DavisRFM69 radio;
RTC_Millis rtc;  // software clock
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085); // Barometric pressure sensor, uses I2C. http://www.adafruit.com/products/1603

// Function Prototypes
bool getWirelessData();
void processPacket();
bool uploadWeatherData();
void updateRainAccum();
void updateRainAccum2();
void updateRainAccum3();
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
time_t getNtpTime();
void sendNTPpacket(IPAddress &address, byte *packetBuff, int PACKET_SIZE);
void softReset();


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
    delay(250);
  }
      
  // Setup Ethernet
  pinMode(ETH_SS_PIN, OUTPUT);
  Ethernet.select(ETH_SS_PIN);  // Set slave select pin - requires modified Ethernet.h library
  Ethernet.begin(g_mac, g_ip);  // Would prefer not to use DHCP because it takes up a lot of memory, but it's not working with the WIZ811MJ
  Serial.println(Ethernet.localIP());
  Udp.begin(8888);  // local port 8888 to listen for UDP packet for NTP time server
  
  // Get time from NTP server
  time_t t = getNtpTime();
  if ( t != 0 )
  { rtc.begin(DateTime(t)); }
 
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

  Serial.print(F("Weather Station "));
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
  
  bool haveFreshWeatherData = getWirelessData();
  
  // Send data to Weather Underground PWS
  bool isTimeToUpload = (long)(millis() - uploadTimer) > 0;
  bool isNptTimeOk = (now.year() >= 2014);
  if( isTimeToUpload && isNptTimeOk && haveFreshWeatherData && g_gotInitialWeatherData )
  {
    // Get date that's not from outside weather station
    g_dewpoint = dewPoint( (float)g_outsideTemperature/10.0, g_outsideHumidity);
    updateBaromoter();  
    updateInsideTemp();

    uploadWeatherData();
    uploadTimer = millis() + 60000; // upload again in 60 seconds
  }
  
  // Reboot if millis is close to rollover. RTC_Millis won't work properly if millis rolls over.  Takes 49 days
  if( millis() > 4294000000UL )
  { softReset(); }
  
}  // end loop()


// Read wireless data coming from Davis ISS weather station
bool getWirelessData()
{
  
  static uint32_t lastRxTime = 0;
  static byte hopCount = 0;
  
  bool gotGoodData = false;
  
  // Process packet
  if ( radio.receiveDone() )
  {
    packetStats.packetsReceived++;
    // check CRC
    unsigned int crc = radio.crc16_ccitt(radio.DATA, 6);
    if ((crc == (word(radio.DATA[6], radio.DATA[7]))) && (crc != 0))
    {
      processPacket();
      packetStats.receivedStreak++;
      hopCount = 1;
      blink(RX_OK, 50);
      gotGoodData = true;
    }
    else
    {
      packetStats.crcErrors++;
      packetStats.receivedStreak = 0;
      blink(RX_BAD, 50);
    }
    
    #ifdef PRINT_DEBUG
//      Serial.print(F("ISS ID ")); 
//      Serial.println((radio.DATA[0] & 0x07) + 1); 
//      Serial.print(F("ch: "));
//      Serial.println(radio.CHANNEL);
//      Serial.print(F("Data: "));
//      for (byte i = 0; i < DAVIS_PACKET_LEN; i++)
//      {
//        Serial.print(radio.DATA[i], HEX);
//        Serial.print(F(" "));
//      }
//      Serial.println();
//      Serial.print(F("RSSI: "));
//      Serial.println(radio.RSSI);
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
  const byte SENSOR_OFFLINE = 0xFF;
  
  // Flags are set true as each variable comes in for the first time
  static bool gotTempData = false;
  static bool gotHumidityData = false; 
  static bool gotRainData = false; 
  float uvi = 0.0; // UV incex
  float sol = 0.0; // Solar radiatioin
  uint16_t tt = 0; // temporary variable calculations
  uint16_t rainSeconds = 0;  // seconds between rain bucket tips

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
  
  // Look at MSB in firt byte to get data type
  byte byte4MSN;  // Holds MSB of byte 4 - used for seconds between bucket tips - maybe
  switch (radio.DATA[0] >> 4)
  {

  case ISS_OUTSIDE_TEMP:
    g_outsideTemperature = (int16_t)(word(radio.DATA[3], radio.DATA[4])) >> 4;
    gotTempData = true;  // one-time flag when data first arrives.  Used to track when all the data has arrived and can be sent to PWS  
    #ifdef PRINT_DEBUG
      Serial.print(F("Outside Temp: "));
      Serial.println(g_outsideTemperature);
    #endif
    break;

  case ISS_HUMIDITY:
    // Round humidity to nearest integer
    g_outsideHumidity = (byte) ( (float)( word((radio.DATA[4] >> 4), radio.DATA[3]) ) / 10.0 + 0.5 );
    gotHumidityData = true;  // one-time flag when data first arrives
    #ifdef PRINT_DEBUG
      Serial.print(F("Humidity: "));
      Serial.println(g_outsideHumidity);
    #endif
    break;
    
  case ISS_RAIN: 
    g_rainCounter =  radio.DATA[3];
    if ( g_initialRainReading == NO_READING )
    { g_initialRainReading = FIRST_READING; } // got first rain reading
    else if ( g_initialRainReading == FIRST_READING )
    { g_initialRainReading = AFTER_FIRST_READING; } // already had first reading, now it's everything after the first time
    gotRainData = true;   // one-time flag when data first arrives
    updateRainAccum2();
    #ifdef PRINT_DEBUG
      Serial.print(F("Rain Cnt: "));
      Serial.println(g_rainCounter);
    #endif
    break;

  case ISS_RAIN_SECONDS:  // Seconds between bucket tips, used to calculate rain rate.  See: http://www.wxforum.net/index.php?topic=10739.msg190549#msg190549
    // srg - still trying to figure this out
    byte4MSN = radio.DATA[4] >> 4;
    if ( byte4MSN < 4 )
    { rainSeconds = radio.DATA[3] >> 4; }  // seconds between bucket tips is byte 3 MSB
    else
    { rainSeconds = radio.DATA[3] + ((byte4MSN - 4) * 255); } // seconds between tips is byte 3 + (byte 4 MSB - 4) * 255
    #ifdef PRINT_DEBUG
      Serial.print(F("Rain Rate testing:\t"));
      Serial.print(rainSeconds);
      Serial.print("\t");
      Serial.print(millis()/1000);
      Serial.print("\t");
      Serial.print(g_rainCounter);
      Serial.print("\t");
      Serial.print(radio.DATA[3], HEX);
      Serial.print("\t");
      Serial.print(radio.DATA[4], HEX);
      Serial.println();
      updateRainAccum3( rainSeconds );  
      printStrm();
    #endif
    break;
      
  case ISS_SOLAR_RAD:  // Calculation source: http://www.wxforum.net/index.php?topic=18489.msg178506#msg178506
    if ( radio.DATA[3] != SENSOR_OFFLINE )
    {
      tt = word(radio.DATA[3], radio.DATA[4]);
      tt = tt >> 4;
      sol = (float)(tt - 4) / 2.27 - 0.2488;
    }
    else
    { sol = 0.0; }
    #ifdef PRINT_DEBUG
      Serial.print(F("Solar: "));
      Serial.println(sol);
      printStrm();
    #endif
    break;
     
  case ISS_UV_INDEX:  // Calculation source: http://www.wxforum.net/index.php?topic=18489.msg178506#msg178506
    if ( radio.DATA[3] != SENSOR_OFFLINE )
    {
      tt = word(radio.DATA[3], radio.DATA[4]);
      tt = tt >> 4;
      uvi = (float)(tt-4) / 200.0;
    }
    else
    { uvi = 0.0; }
    #ifdef PRINT_DEBUG
      Serial.print(F("UV Index: "));
      Serial.println(uvi);
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
  { g_gotInitialWeatherData = true; }
  
} //  end processPacket()


// Upload to Weather Underground
bool uploadWeatherData()
{
   // Get UTC time and format
  char dateutc[25];
  getUtcTime(dateutc);  

  // Send the Data to weather underground
  if (client.connect(g_server, 80))
  {
    client.print("GET /weatherstation/updateweatherstation.php?ID=");
    client.print(WUNDERGROUND_STATION_ID);
    client.print("&PASSWORD=");
    client.print(WUNDERGROUND_PWD);
    client.print("&dateutc=");
    client.print(dateutc);
    client.print("&winddir=");
    client.print(g_windDirection);
    client.print("&windspeedmph=");
    client.print(g_windSpeed);
    client.print("&windgustmph=");
    client.print(g_windgustmph);
    if ( g_outsideTemperature > -400 && g_outsideTemperature < 1300 )  // temp must be between -40 F and 130 F
    {
      client.print("&tempf=");
      client.print((float)g_outsideTemperature / 10.0);
    }
    if ( g_insideTemperature > 35 )  // Inside temp must be > 35 F
    {
      client.print("&indoortempf=");
      client.print(g_insideTemperature);
    }
    client.print("&rainin=");
    client.print((float)g_rainRate / 100.0);  // rain inches over the past hour
    client.print("&dailyrainin=");   // rain inches so far today in local time
    client.print((float)g_dayRain / 100.0);  //
    if ( g_barometer > 20000L && g_barometer < 40000L )
    {
      client.print("&baromin=");
      client.print((float)g_barometer / 1000.0);
    }
    client.print("&dewptf=");
    client.print(g_dewpoint);
    if (g_outsideHumidity <= 100 )
    { 
      client.print("&humidity=");
      client.print(g_outsideHumidity);
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
      return false;
    }    
  }  // end while (client.connected() )
  
  client.stop();
  blink(TX_OK, 50);
  return true;
  
} // end uploadWeatherData()


// Once a minute calculates rain in inches per hour
// by looking at the rain for the last 15 minutes, and multiply by 4
// Also track rain accumulation for the day.  Reset at midnight (local time, not UDT time)
// Everything is in 0.01" units of rain
void updateRainAccum()
{
  // For the first time program receives the rain counter value, we need 
  // to set the prevRainCnt to match so we don't count the starting point as rain that came in on this minute 
  static byte prevRainCounter = 0;  // rain count (not incremental) from previous minute
  
  // If program is booting up, set previous rain counter to current counter so it's not counting rain that's not real
  if ( g_initialRainReading == FIRST_READING )
  { prevRainCounter = g_rainCounter; }
  
  static byte rainEachMinute[15];  // array holds incremental rain for a 15 minute period
  static byte minuteIndex = 0;     // index for rainEachMinute
  if ( isNewMinute() )
  {
    // Calculate new rain since since the last minute
    byte newRain = 0; // incremental new rain since last minute
    if ( g_rainCounter < prevRainCounter )
    { newRain = (128 - prevRainCounter) + g_rainCounter; } // variable has rolled over
    else
    { newRain = g_rainCounter - prevRainCounter; }
    
    rainEachMinute[minuteIndex++] = newRain;
    if ( minuteIndex == 15 )
    { minuteIndex = 0; }
    
    // calculate hourly rain rate
    uint16_t sumRain = 0;
    for (byte i = 0; i < 15; i++)
    { sumRain += rainEachMinute[i]; }
    g_rainRate = sumRain * 4;
    
    prevRainCounter = g_rainCounter;
    
    // Increment daily rain counter
    g_dayRain += newRain;
  }
  
  // reset daily rain accumulation
  if ( isNewDay() )
  { g_dayRain = 0; }
  
} // end updateRainAccum()


// Different rain accumulation formula
// Calculates hourly rain rate by looking at the seconds between bucket tips
// Time is based when transmission is made, not actual seconds between bucket tips
// Reset rate to zero after 15 minutes of no rain
// Need at least two bucket tips to make calculation
// srgg
// see http://www.wxforum.net/index.php?topic=23652.msg227558#msg227558
void updateRainAccum2()
{
  const uint32_t FIFTEEN_MIN = 900000;  
  DateTime now = rtc.now();

  static time_t   rainTimePrev =       0; // Time stamp for previous rain bucket tip
  static byte     newRain =            0; // incremental new rain since last bucket tip
  static byte     prevRainCounter =    0; // previous ISS rain counter value
  static uint32_t secondsBetweenTips = 0;
  
  // If program has recently restarted, set previous rain counter to current counter
  if ( g_initialRainReading == FIRST_READING )
  {  prevRainCounter = g_rainCounter; }

  // If the ISS rain counter changed since the last transmission, then update timestamps
  if ( g_rainCounter != prevRainCounter )
  {
    // See how many bucket tips counter went up.  Should be only one unless it's raining really hard or there is a long transmission delay from ISS
    if ( g_rainCounter < prevRainCounter )
    { newRain = (128 - prevRainCounter) + g_rainCounter; } // ISS rain counter has rolled over (counts from 0 - 127)
    else
    { newRain = g_rainCounter - prevRainCounter; }
    
    // Calculate seconds between bucket tips
    secondsBetweenTips = now.unixtime() - rainTimePrev;
    rainTimePrev = now.unixtime();  

    // Increment daily rain counter
    g_dayRain += newRain;
  }  
  else if ( (now.unixtime() - rainTimePrev) > secondsBetweenTips )
  {
    // Keep the rain rate constant for the length of time that's currently the seconds between bucket tips, then start to drop the rate
    // For example, if time between bucket tips is 100 seconds, don't start to drop the rain rate until 100 seconds have passed since the last bucket tip
    secondsBetweenTips = now.unixtime() - rainTimePrev;
  }

  // calculate hourly rain rate in 0.01"/minute
  // One bucket tip is 0.01" rain
  if ( secondsBetweenTips > 0  && secondsBetweenTips <= FIFTEEN_MIN )
  { g_rainRate = newRain * ( 3600 / secondsBetweenTips ); }
  else 
  { g_rainRate = 0; }  // More then 15 minutes since last bucket tip, can't calculate rain rate until next bucket tip

  prevRainCounter = g_rainCounter; 
  
  // reset daily rain accumulation
  if ( isNewDay() )
  { g_dayRain = 0; }

// srg debug
Serial.print(F("Sec between bucket tips: "));
Serial.print(secondsBetweenTips);
Serial.print("\t\t rain rate: ");
Serial.println(g_rainRate);

}  // end updateRainAccum2()


// Calculate rain rate per hour from seconds between bucket tips
// Seconds is sent from ISS
// SRG - still trying to decode bucket tip seconds from ISS
void updateRainAccum3( uint16_t rainSeconds )
{
  uint16_t rainRateTest = 0;
  if ( rainSeconds <= 900 && rainSeconds > 0 )
  { rainRateTest = 3600 / rainSeconds; }
  else
  { rainRateTest = 0; }
  
}  // end updateRainAccum3()


// Update wind data
// Below are all the Weather Underground wind variables.  This program only uses: winddir, windspeedmph, windgustmph
// windgustmph is over 5 minutes, then resets to current wind speed 
// winddir is averaged of the last 10 readings
// windspeedmph is the current wind speed
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

  g_windSpeed = currentWindSpeedMph;  // just use current wind, no smoothing or anything
  
  // Calculate wind gusts - max over 5 minutes
  if ( (long)(millis() - timer_Wind_Gust) > 0 )
  {
    g_windgustmph = currentWindSpeedMph;  // reset wind gust
    timer_Wind_Gust = millis() + FIVE_MINUTES;
  }
  else
  {
    if ( currentWindSpeedMph > g_windgustmph )
    { g_windgustmph = currentWindSpeedMph; }
  }

  // For for wind direction calculation, use a smoothing algorithm
  // To avoid problem of direction crossing over from a high number like 350 to a low number like 3, add 1000 do direction, 
  // smooth it, then subtract 1000.  Wind direction goes from 1 to 360
  if ( g_windDirection == 0 )
  { g_windDirection = windDir; } // No wind data calculated yet, set initial conditions
  float windDirSmoothingOld = g_windDirection + 1000.0;
  float windDirSmoothingNew =         windDir + 1000.0;
  float filterVal = 0.05; 
  g_windDirection = (windDirSmoothingOld * (1.0 - filterVal)) + ( windDirSmoothingNew * filterVal);
  g_windDirection = g_windDirection - 1000; // convert back to 1-360 range

/* old wind direction Calc - averaged most recent 10 readings
  const byte NUM_WIND_READINGS = 10;
  static uint16_t windDirData[NUM_WIND_READINGS];
  static byte index = 0;
  
  windDirData[index++] = windDir;
  if ( index >= NUM_WIND_READINGS )
  { index = 0; }
  
  float sumDirection = 0.0;
  for ( byte i = 0; i < NUM_WIND_READINGS; i++ )
  { sumDirection += windDirData[i]; }

  g_windDirection = ( sumDirection / (float)NUM_WIND_READINGS ) + 0.5;
*/

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
    float pressureInHg = event.pressure * 29.53 ;  // convert hPa to inches of mercury * 1000
    float temperatureC;
    bmp.getTemperature(&temperatureC);
    float temperatureK = temperatureC = 273.15;
    g_barometer = pressureInHg * pow(2.71828182, ALTITUDE / (29.3 * temperatureK));
    return true;
  }
  else
  { return false; }
 
} // end updateBaromoter()


// Inside temperature from BMP180 sensor
bool updateInsideTemp()
{
  bmp.getTemperature(&g_insideTemperature);
  g_insideTemperature = g_insideTemperature * 1.8 + 32.0;  // convert to F 
  if (g_insideTemperature > 35 && g_insideTemperature < 110) // Validate indoor temperature range
  {  return true; }
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



void printFreeRam() 
{
  extern int __heap_start, *__brkval;
  int v;
  Serial.print(F("Free mem: "));
  Serial.println((int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval));
} // end printFreeRam()


time_t getNtpTime()
{
  const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
  byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming & outgoing packets
  
  
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  { sendNTPpacket(timeServer, packetBuffer, NTP_PACKET_SIZE); }
  
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
void sendNTPpacket(IPAddress &address, byte *packetBuff, int PACKET_SIZE)
{
  // set all bytes in the buffer to 0
  memset(packetBuff, 0, PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuff[0] = 0b11100011;   // LI, Version, Mode
  packetBuff[1] = 0;     // Stratum, or type of clock
  packetBuff[2] = 6;     // Polling Interval
  packetBuff[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuff[12]  = 49;
  packetBuff[13]  = 0x4E;
  packetBuff[14]  = 49;
  packetBuff[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuff, PACKET_SIZE);
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
}  // end softReset()

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



