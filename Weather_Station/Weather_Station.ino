/*
Upload sketch: Select Uno as a board.  Need FTDI cable

See Github repository for more information: http://git.io/Ds7oxw
Test weather station http://www.wunderground.com/personal-weather-station/dashboard?ID=KVTDOVER3

To Do:
Automatically switch to alternate time server if main one deosn't work
If no WU uploads in 10 minutes, reboot
On reboot, query WU and get starting rain accumulation for the day

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
                 Changed upload time from 20 to 60 seconds.  Added pinMode() for the SS pin, use static ip instead of dynamic
09/14/14 v0.25   Updated Ethernet.cpp library so I don't need pinMode() for the SS pin
10/04/14 v0.26   Changed rain rate so it used data from ISS.
10/06/14 v0.27   Added PRINT_DEBUG_WU_UPLOAD so I can only monitor Ethernet uploads and not fill serial monitor with other text
10/06/14 v0.28   Get wind gust from ISS instead of calculating.  Changed Solar rad formula, but that's not used by me
10/07/14 v0.29   Fixed bug in rain accumulation. Fixed wind speed bug in v.028
10/08/14 v.030   Removed unused rain rate code.  Small general clean-up
10/14/14 v0.31   Removed smoothing of wind direction, formula was invalid
10/15/14 v0.32   Use vector averaging for wind direction
10/20/14 v0.33   Changed wind direction formula to get rid of dead zone, see http://bit.ly/1uxc9sf
10/24/14 v.034   Tweaked rain rate formula based on results from last test and fixed wind direction rollover

*/

#define VERSION "v0.34" // version of this program
//#define PRINT_DEBUG     // comment out to remove many of the Serial.print() statements
#define PRINT_DEBUG_WU_UPLOAD // prints out messages related to Weather Underground upload.  Comment out to turn off
//#define PRINT_DEBUG_RAIN // prints out rain rate testing data

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
const byte TRANSMITTER_STATION_ID = 3; // ISS station ID to be monitored.  Default station ID is normally 1

// Reduce number of bogus compiler warnings, see http://bit.ly/1pU6XMe
#undef PROGMEM
#define PROGMEM __attribute__(( section(".progmem.data") ))

#if !defined(__time_t_defined)
  typedef unsigned long time_t;
#endif

// Header bytes from ISS (weather station tx) that determine what measurement is being sent
// Valid hex values are: 40 50 60 80 90 A0 E0  - really only care about MSB, the LSB is staion ID (where 0 is station #1)
// See http://github.com/dekay/im-me/blob/master/pocketwx/src/protocol.txt
//     http://bit.ly/1kDsXK4
//     http://www.wxforum.net/index.php?topic=10739.msg190549#msg190549
//     http://www.wxforum.net/index.php?topic=18489.msg178506#msg178506
#define ISS_UV_INDEX     0x4
#define ISS_RAIN_SECONDS 0x5
#define ISS_SOLAR_RAD    0x6
#define ISS_OUTSIDE_TEMP 0x8
#define ISS_WIND_GUST    0x9
#define ISS_HUMIDITY     0xA
#define ISS_RAIN         0xE

char g_server [] = "weatherstation.wunderground.com";  // standard weather underground server

// Ethernet and time server setup
IPAddress g_timeServer( 132, 163, 4, 101 );   // ntp1.nl.net NTP server
// IPAddress timeServer (132, 163, 4, 102); // time-b.timefreq.bldrdoc.gov NTP server
// IPAddress timeServer (132, 163, 4, 103);
// IPAddress timeServer (192, 43, 244, 18); // time.nist.gov NTP server

byte g_mac[] = { 0xDE, 0xAD, 0xBD, 0xAA, 0xAB, 0xA4 };
byte g_ip[] = { 192, 168, 46, 85 };   // Static IP on LAN

// Weather data to send to Weather Underground
byte     g_rainCounter =        0;  // rain data sent from outside weather station.  1 = 0.01".  Just counts up to 127 then rolls over to zero
byte     g_windgustmph =        0;  // Wind in MPH
float    g_dewpoint =           0.0;  // Dewpoint F
uint16_t g_dayRain =            0;  // Accumulated rain for the day in 1/100 inch
float    g_rainRate =           0;  // Rain rate in inches per hour
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
const byte MOTEINO_LED = 9;  // PCB LED on Moteino, only used to flash at startup
const byte ETH_SS_PIN =  7;  // Slave select for Ethernet module

// Used to track first couple of rain readings so initial rain counter can be set
enum rainReading_t { NO_READING, FIRST_READING, AFTER_FIRST_READING };
rainReading_t g_initialRainReading = NO_READING;  // Need to know when very first rain counter reading comes so inital calculation is correct  

// Instantiate class objects
EthernetClient client;
EthernetUDP Udp;       // UDP for the time server
DavisRFM69 radio;
RTC_Millis rtc;        // software clock
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085); // Barometric pressure sensor, uses I2C. http://www.adafruit.com/products/1603

// Function Prototypes
bool getWirelessData();
void processPacket();
bool uploadWeatherData();
void updateRainAccum();
void updateRainRate(uint16_t rainSeconds);
void avgWindDir(float windDir);
bool updateBaromoter();
bool updateInsideTemp();
float dewPointFast(float tempF, byte humidity);
float dewPoint(float tempf, byte humidity);
void printFreeRam();
void printPacket();  // prints packet from ISS in hex
void blink(byte PIN, int DELAY_MS);
void getUtcTime(char timebuf[]);
bool isNewDay();
time_t getNtpTime();
void sendNTPpacket(IPAddress &address, byte *packetBuff, int PACKET_SIZE);
void softReset();


void setup() 
{
  Serial.begin(9600);
  delay(4000); 
  Serial.print(F("Weather Station "));
  Serial.println(VERSION);

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
  Ethernet.select(ETH_SS_PIN);  // Set slave select pin - requires modified Ethernet.h library
  Ethernet.begin(g_mac, g_ip);  
  Serial.println(Ethernet.localIP());
  Udp.begin(8888);  // local port 8888 to listen for UDP packet for NTP time server
  
  // Get time from NTP server
  time_t t = getNtpTime();
  if ( t != 0 )
  { rtc.begin(DateTime(t)); }
  else
  { Serial.println(F("Did not get NTP time")); }
  
  // Set up BMP085 pressure and temperature sensor
  if (!bmp.begin())
  { Serial.println(F("Could not find BMP180")); }

  // Setup Moteino radio
  radio.initialize();
  radio.setChannel(0); // Frequency - Channel is *not* set in the initialization. Need to do it now

  printFreeRam();
  Serial.println();
  
}  // end setup()


void loop()
{
  static uint32_t uploadTimer = 0; // timer to send data to weather underground
  DateTime now = rtc.now();
  
  bool haveFreshWeatherData = getWirelessData();
   
  // Send data to Weather Underground PWS.  Normally takes about 700mS
  bool isTimeToUpload = (long)(millis() - uploadTimer) > 0;
  bool isNptTimeOk = (now.year() >= 2014);
  if( isTimeToUpload && isNptTimeOk && haveFreshWeatherData && g_gotInitialWeatherData )
  {
    // Get data that's not from outside weather station
    g_dewpoint = dewPoint( (float)g_outsideTemperature/10.0, g_outsideHumidity);
    updateBaromoter();  
    updateInsideTemp();

    uploadWeatherData();
    uploadTimer = millis() + 60000; // upload again in 60 seconds
  }
  
  // Reboot if millis is close to rollover. RTC_Millis won't work properly if millis rolls over.  Takes 49 days to rollover
  if( millis() > 4294000000UL )
  { softReset(); }
  
}  // end loop()


// Read wireless data coming from Davis ISS weather station
bool getWirelessData()
{
  
  static uint32_t lastRxTime = 0;  // timestamp of last received data.  Doesn't have to be good data
  static byte hopCount = 0;
  
  bool gotGoodData = false;
  
  // Process ISS packet
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
      if ( g_initialRainReading == FIRST_READING)
      {
        Serial.print(F("ISS ID "));
        Serial.println((radio.DATA[0] & 0x07) + 1);
        Serial.print(F("ch: "));
        Serial.println(radio.CHANNEL);
        Serial.print(F("RSSI: "));
        Serial.println(radio.RSSI);
      }
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
    
    if (hopCount == 1)
    { packetStats.numResyncs++; }
    
    if (++hopCount > 25)
    { hopCount = 0; }
    
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
  float uvi = 0.0; // UV index
  float sol = 0.0; // Solar radiatioin
  uint16_t tt = 0; // temporary variable for calculations
  uint16_t rainSeconds = 0;  // seconds between rain bucket tips
  byte byte4MSN;  // Holds MSB of byte 4 - used for seconds between bucket tips

  // station ID - the low order three bits are the station ID.  Station ID 1 is 0 in this data
  byte stationId = (radio.DATA[0] & 0x07) + 1;
  if ( stationId != TRANSMITTER_STATION_ID )
  { return; }  // exit function if this isn't the station ID program is monitoring

  // Every packet has wind speed, wind direction and battery status in it
  g_windSpeed = radio.DATA[1];  
  #ifdef PRINT_DEBUG
    Serial.print(F("Wind Speed: "));
    Serial.println(g_windSpeed);
  #endif

  // There is a dead zone on the wind vane. No values are reported between 8
  // and 352 degrees inclusive. These values correspond to received byte
  // values of 1 and 255 respectively
  // See http://www.wxforum.net/index.php?topic=21967.50
//  float windDir = 9 + radio.DATA[2] * 342.0f / 255.0f; - formula has dead zone from 352 to 10 degrees
  float windDir; 
  if ( radio.DATA[2] == 0 )
  { windDir = 0; }
  else 
  { windDir = ((float)radio.DATA[2] * 1.40625) + 0.3; }  // This formula doesn't have dead zone, see: http://bit.ly/1uxc9sf
  avgWindDir( windDir );  // Average out the wind direction with vector math
  #ifdef PRINT_DEBUG
    Serial.print(F("Wind Direction: "));
    Serial.print(windDir);
    Serial.print("\t");
    Serial.print(g_windDirection);
    Serial.println();
  #endif

  // 0 = battery ok, 1 = battery low.  Not used by anything in program
  byte transmitterBatteryStatus = (radio.DATA[0] & 0x8) >> 3; 
  
  // Look at MSB in firt byte to get data type
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
  
  case ISS_WIND_GUST:
    g_windgustmph = radio.DATA[3];
    #ifdef PRINT_DEBUG
      Serial.print(F("Wind Gust: "));
      Serial.println(g_windgustmph);
    #endif
    break;
      
  case ISS_RAIN: 
    g_rainCounter = radio.DATA[3];
    if ( g_initialRainReading == NO_READING )
    { g_initialRainReading = FIRST_READING; } // got first rain reading
    else if ( g_initialRainReading == FIRST_READING )
    { g_initialRainReading = AFTER_FIRST_READING; } // already had first reading, now it's everything after the first time
    gotRainData = true;   // one-time flag when data first arrives

    updateRainAccum();
    
    #ifdef PRINT_DEBUG_RAIN
      Serial.print(F("Rain Cnt: "));
      Serial.print(g_rainCounter);
      Serial.print("\t");
      Serial.print(radio.DATA[3]);
      Serial.println(); 
      printPacket();
    #endif
    break;

  case ISS_RAIN_SECONDS:  // Seconds between bucket tips, used to calculate rain rate.  See: http://www.wxforum.net/index.php?topic=10739.msg190549#msg190549
    byte4MSN = radio.DATA[4] >> 4;
    if ( byte4MSN < 4 )
    { rainSeconds =  (radio.DATA[3] >> 4) + radio.DATA[4] - 1; }  
    else
    { rainSeconds = radio.DATA[3] + (byte4MSN - 4) * 256; }   

    updateRainRate(rainSeconds);
    
    #ifdef PRINT_DEBUG_RAIN
      Serial.print(F("Rain Rate testing:\t"));
      Serial.print(rainSeconds);
      Serial.print("\t");
      Serial.print(millis()/1000);
      Serial.print("\t");
      Serial.print(g_rainCounter);
      Serial.print("\t");
      Serial.print(radio.DATA[3], HEX);
      Serial.print(" ");
      Serial.print(radio.DATA[4], HEX);
      Serial.println();
    #endif
    break;
      
  case ISS_SOLAR_RAD:
    if ( radio.DATA[3] != SENSOR_OFFLINE )
    {
      // Calculation source: http://www.wxforum.net/index.php?topic=18489.msg178506#msg178506
      // tt = word(radio.DATA[3], radio.DATA[4]);
      // tt = tt >> 4;
      // sol = (float)(tt - 4) / 2.27 - 0.2488;
      
      sol = (radio.DATA[3] * 4) + ((radio.DATA[3] && 0xC0) / 64 );  // Another source: http://www.carluccio.de/davis-vue-hacking-part-2/
    }
    else
    { sol = 0.0; }
    #ifdef PRINT_DEBUG
      Serial.print(F("Solar: "));
      Serial.println(sol);
      printPacket();
    #endif
    break;
     
  case ISS_UV_INDEX:
    if ( radio.DATA[3] != SENSOR_OFFLINE )
    {
      // Calculation source: http://www.wxforum.net/index.php?topic=18489.msg178506#msg178506
      tt = word(radio.DATA[3], radio.DATA[4]);
      tt = tt >> 4;
      uvi = (float)(tt-4) / 200.0;
    }
    else
    { uvi = 0.0; }
    #ifdef PRINT_DEBUG
      Serial.print(F("UV Index: "));
      Serial.println(uvi);
      printPacket();
    #endif
    break;

  default:
    #ifdef PRINT_DEBUG
      Serial.print(F("Other header: "));
      printPacket();
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
  uint32_t uploadApiTimer = millis();  // Used to time how long it takes to upload to WU

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
    client.print(g_rainRate);  // rain inches per hour
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
    #ifdef PRINT_DEBUG_WU_UPLOAD
      Serial.println(F("\nWU connection failed"));
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
      #ifdef PRINT_DEBUG_WU_UPLOAD
        Serial.print(c);
      #endif
    }  
    
    connectLoopCounter++;
    // if more than 10000 loops since the last packet, then timeout
    if( connectLoopCounter > 10000L )
    {
      client.stop();
      #ifdef PRINT_DEBUG_WU_UPLOAD
        Serial.println(F("\n\nEthernet Timeout. Waited "));
        Serial.print((long)(millis() - uploadApiTimer));
        Serial.println(" mS");
      #endif
      return false;
    }    
  }  // end while (client.connected() )
  
  client.stop();
  blink(TX_OK, 50);
  #ifdef PRINT_DEBUG_WU_UPLOAD
    if ( (long)(millis() - uploadApiTimer) > 1500 )
    {
      Serial.print(F("WU upload took (mS): "));
      Serial.println((long)(millis() - uploadApiTimer));
    }
  #endif
  return true;
  
} // end uploadWeatherData()


// Track daily rain accumulation.  Reset at midnight (local time, not UDT time)
// Everything is in 0.01" units of rain
void updateRainAccum()
{
  
  static byte newRain =         0; // incremental new rain since last bucket tip
  static byte prevRainCounter = 0; // previous ISS rain counter value
  
  // If program has recently restarted, set previous rain counter to current counter
  if ( g_initialRainReading == FIRST_READING )
  {  prevRainCounter = g_rainCounter; }
 
  // If the ISS rain counter changed since the last transmission, then update rain accumumulation
  if ( g_rainCounter != prevRainCounter )
  {
    // See how many bucket tips counter went up.  Should be only one unless it's raining really hard or there is a long transmission delay from ISS
    if ( g_rainCounter < prevRainCounter )
    { newRain = (128 - prevRainCounter) + g_rainCounter; } // ISS rain counter has rolled over (counts from 0 - 127)
    else
    { newRain = g_rainCounter - prevRainCounter; }

    // Increment daily rain counter
    g_dayRain += newRain;
  }
  
  prevRainCounter = g_rainCounter;
  
  // reset daily rain accumulation
  if ( isNewDay() )
  { g_dayRain = 0; }
  
} // end updateRainAccum()


// Calculate rain rate in inches/hour
// rainSeconds seconds is sent from ISS.  It's the number fo seconds since the last bucket tip
void updateRainRate( uint16_t rainSeconds )
{
  if ( rainSeconds < 1020 )
  { g_rainRate = 36.0 / (float)rainSeconds; }
  else
  { g_rainRate = 0.0; }  // More then 15 minutes since last bucket tip, can't calculate rain rate until next bucket tip
  
}  // end updateRainRate()


// Calculate average of wind direction
// Because wind direction goes from 359 to 0, use vector averaging to determine direction
void avgWindDir(float windDir)
{
  const byte ARYSIZE = 30;     // number of elements in arrays
  const float DEG2RAD = 3.14156 / 180.0; // convert degrees to radian
  static float dirNorthSouth[ARYSIZE];
  static float dirEastWest[ARYSIZE];
  static byte c = 0; // array counter
  
  if ( c == ARYSIZE )
  { c = 0; }
  
  windDir = windDir + 1; // convert range from 0-359 to 1 to 360

  dirNorthSouth[c] = cos(windDir * DEG2RAD);
  dirEastWest[c++] = sin(windDir * DEG2RAD);
  
  // Get array totals
  float sumNorthSouth = 0.0;
  float sumEastWest =   0.0;
  for (byte i = 0; i < ARYSIZE; i++)
  { 
    sumNorthSouth += dirNorthSouth[i];
    sumEastWest +=   dirEastWest[i];
  }
  
  float avgWindDir = atan2( (sumEastWest/(float) ARYSIZE), (sumNorthSouth/(float) ARYSIZE));
  avgWindDir = avgWindDir / DEG2RAD;  // convert radians back to degrees
  
  if ( avgWindDir < 0 )
  { avgWindDir += 360; }

  g_windDirection = (int)avgWindDir % 360; // atan2() result can be > 360, so use modulus to just return remainder
  
  if (g_windDirection >=1 )
  { g_windDirection = g_windDirection - 1; } // convert range back to 0-359
   
}  // end avgWindDir()


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


// Calculate the dew point, this is supposed to be the fast version
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


// Another dewpoint calculation - slower
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
  { sendNTPpacket(g_timeServer, packetBuffer, NTP_PACKET_SIZE); }
  
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

// print ISS data packet
void printPacket() 
{
  for (byte i = 0; i < DAVIS_PACKET_LEN; i++) 
  {
    Serial.print(radio.DATA[i], HEX);
    Serial.print(F(" "));
  }
  Serial.println();
} // end printPacket



