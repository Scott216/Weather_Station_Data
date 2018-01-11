/*
Board type: 
Moteino 
To add Moteino to board list, in Arduino preferences, enter this URL
https://lowpowerlab.github.io/MoteinoCore/package_LowPowerLab_index.json

Changes to w5100 library so you can change slave select pin;
https://github.com/Scott216/Ethernet_SS_Mod



See Github repository for more information: http://git.io/Ds7oxw
Test weather station   http://www.wunderground.com/personal-weather-station/dashboard?ID=KVTDOVER3
Suntec weather station http://www.wunderground.com/personal-weather-station/dashboard?ID=KVTWESTD3

Weather Underground Wiki: http://wiki.wunderground.com/index.php/PWS_-_Upload_Protocol#GET_parameters

Issues:
Radio frequenlty has to re-sync frequency hopping timing - this can be slow

To Do:

To Try:
- Only get NTP time on startup
- Keep WDT going unless there is a CRC error. Radio data normally takes 2.5 seconds to get, which won't trigger WDT.  But when a CRC error comes in, data takes about 70 seconds to get
- Maybe usingInterrupt() can help https://www.arduino.cc/en/Reference/SPIusingInterrupt
- Also try using latest Ethernet and W5100.h libraries

 
I/O for Moteina 
A4,A5 - I2C communication with MPB180 sensor
A0 - Rx Good LED (green) - good packet received by Moteino
A1 - Rx Bad LED (red) - bad packet received by Moteino
D3 - Tx Good LED (green) - good upload to weather underground
D7 - Slave select for Ethernet module. For port registers, this is Port D, bit 7 on Moteino, not sure about MoteinoMega
D8 - Slave Select for Flash chip if  you got the Flash chip option on your Moteino
D9  LED mounted on the Moteino PCB 
D2, D10-13 - used by Moteino radio transciever. D10 is radio slave select


Change log:
07/23/14 v0.12 - Fixed isNewDay bug.  Averaged wind data and increased wind gust period to 5 minutes.  
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
09/12/14 v0.24 - Removed last good packet timer.  Enabled rain rate calculation based on seconds between bucket tips
                 Moved packetBuffer & NTP_PACKET_SIZE from global variables to local.  These are passed as parameters to sendNTPpacket()
                 Changed upload time from 20 to 60 seconds.  Added pinMode() for the SS pin, use static ip instead of dynamic
09/14/14 v0.25 - Updated Ethernet.cpp library so I don't need pinMode() for the SS pin
10/04/14 v0.26 - Changed rain rate so it used data from ISS.
10/06/14 v0.27 - Added PRINT_DEBUG_WU_UPLOAD so I can only monitor Ethernet uploads and not fill serial monitor with other text
10/06/14 v0.28 - Get wind gust from ISS instead of calculating.  Changed Solar rad formula, but that's not used by me
10/07/14 v0.29 - Fixed bug in rain accumulation. Fixed wind speed bug in v.028
10/08/14 v.030 - Removed unused rain rate code.  Small general clean-up
10/14/14 v0.31 - Removed smoothing of wind direction, formula was invalid
10/15/14 v0.32 - Use vector averaging for wind direction
10/20/14 v0.33 - Changed wind direction formula to get rid of dead zone, see http://bit.ly/1uxc9sf
10/24/14 v0.34 - Tweaked rain rate formula based on results from last test and fixed wind direction rollover
11/29/14 v0.35 - Enabled internal pull-up resistors on SPI slave select pins
12/11/14 v0.36 - Added Reboot counter.
12/18/14 v0.37 - Made Moetino on board LED as a heartbeat
12/23/14 v0.38 - Made reboot counter in EEPROM an integer instead of a byte.  Redid output for PRINT_DEBUG.  Made it a tab delimited table. 
                 On startup get get daily rain accumulation from Weather Underground
12/31/14 v0.39 - Added 2 second delay after Udp.begin to see if it helped reduce serial monitor jibberish
                 Binary sketch size: 29,780 bytes (of a 32,256 byte maximum)
                 Free RAM 445 bytes
01/09/15 v0.40 - Moved reboot address to byte 2 because it wrote 1831 times to address 0
01/10/15 v0.41 - Changed WU Station ID to from test station (KVTDOVER3) to the real station (KVTWESTD3)
01/10/15 v0.42 - Changed getUtcTime() to getTime and added parameter to return UTC or EST time
                 Added WDT (watch dog timer) for WU uplaod.  Can't use it for the radio because radio takes over 8 seconds to lock onto channels
                 30,086 bytes, RAM 423 bytes
01/11/15 v0.43 - Fixed Hautespot (router needed reboot).  Switch this sketch back to test weather station
01/28/15 v0.44 - Added formula for PACKET_INTERVAL.  Changed millis() to micros()/1000 for lastRxTime timer (See: http://bit.ly/1HoH5Hn )  
01/31/15 v0.45 - Added windchill function
03/21/15 v0.46 - Added #ifdef to select MOTEINO_LED & SS_PIN_RADIO for Moteino or MoteinoMega
05/31/15 v0.47 - Added delay in soft reboot so text could print to serial monitor
07/08/15 v0.48 - Disabled getTodayRain(), redid NTP time so it tries twice for each NTP server
07/09/15 v0.49 - Changed processPacket() to decodePacket().  Changed getNtpTime to decodeNTPTime.  Changed getNewNtpTime to loop 3 times for each server
                 Changed getTime() to getFormattedTime()
                 Discovered problems I've been having were due to low battery in ISS tranmitter.  Changed transmitter ID from 3 to 1.
                 Changes to getTodayRain(). Added battery status and CRC errors to printData()
                 Since Hautewind seems dead, I disconnected it and have this sketch pushing data to that station ID
07/27/15 v0.50 - Sends message to serial monitor if no new data is coming through from ISS 
08/02/15 v0.51 - New Dekay RFM69 library. https://github.com/dekay/DavisRFM69
02/03/17 v0.52 - I think WU changed servers to AWS, now uploads don't work.  Fetching daily rain was returning huge numbers, so I set to zero if it's over 10 inches.  
                 Also added print statement when data is uploaded to WU
12/20/17 v0.53 - Changed how NTP works. Instead of using IP addresses for time server, using char timeServer[] = "time.nist.gov";  
12/21/17 v0.54 - Found forum post that explained GET request changes. https://forum.arduino.cc/index.php?topic=461649.msg3199335#msg3199335
12/21/17 v0.55 - Little bit of code cleanup, took out some of the debugging.  Switched back to main staion from test station.
12/22/17 v0.56 - Added #ifdef __AVR_ATmega328P__ to check for Moteino board type.  Compile Size: 28,596 bytes & 1584 bytes RAM
12/22/17 v0.57 - Tyring out kiwisincebrith's w5100.h/cpp files.  His version doesn't change Ethernet.h/cpp.  
                 Added print statement when getting invalid rain data from WU and textfinder. Compile size 28,556, 1582 dynamic (with only setup printing)
12/23/17 v0.58 - Added print statements to where WDT turns on and off to try and see where sketch is hanging.  
                 Modified getTodayRain() to print when textfinder fails. Added for loop to call getTodayRain() up to 10 times
                 Changed NTP time tries from 3 to 20 
12/24/17 v0.59 - Added PRINT_WEATHER_DATA to enable/disable printing weather data table.  Used to be part of PRINT_DEBUG    
12/28/17 v0.60 - Added print when hopCount is reset because it's too high.  Removed isNptTimeOk since I'm using now in upload 
                 Removed g_rainCounter and rainSeconds from printData()
01/11/18 v1.61 - Moved MAC and IP setup into #if defined that deteects Moteino Mega vs standard so IP and MAC are different
*/

#define VERSION "v0.61" // Version of this program

#define PRINT_DEBUG     // Comment out to remove many of the Serial.print() statements
#define PRINT_WEATHER_DATA // Prints weather data table.  Comment out to turn off
#define PRINT_DEBUG_WU_UPLOAD // Prints out messages related to Weather Underground upload. Comment out to turn off
#define PRINT_SETUP_INFO   // Prints which Moteino selected, reboots and free RAM from setip().  Comment out to turn off


#include <DavisRFM69.h>        // http://github.com/dekay/DavisRFM69
#include <Ethernet.h>          // Modified for user selectable SS pin and disables interrupts
#include <utility/W5100.h>     // Needed when using modified w5100.h files from kiwisincebirth
#include <EthernetUdp.h>       // http://github.com/arduino/Arduino/blob/master/libraries/Ethernet/src/EthernetUdp.h
#include <SPI.h>               // DavisRFM69.h needs this   http://arduino.cc/en/Reference/SPI
#include <Wire.h>              // http://github.com/arduino/Arduino/tree/master/libraries/Wire
#include <EEPROM.h>            // http://arduino.cc/en/Reference/EEPROM
#include "RTClib.h"            // http://github.com/adafruit/RTClib
#include "Adafruit_BMP085_U.h" // http://github.com/adafruit/Adafruit_BMP085_Unified
#include "Adafruit_Sensor.h"   // http://github.com/adafruit/Adafruit_Sensor
#include <TextFinder.h>        // http://playground.arduino.cc/Code/TextFinder (used in getTodayRain() from WU response)
#include <avr/wdt.h>           // Watchdog timeout.  Increases progam size by 64 bytes. I din't think it helps much
#include "Tokens.h"            // Holds Weather Underground password


#define WUNDERGROUND_STATION_ID "KVTDOVER3" // Weather Underground station ID - test station
// #define WUNDERGROUND_STATION_ID "KVTWESTD3" // Weather Underground station ID - normal station
const float ALTITUDE = 603.0;               // Altitude of weather station (in meters).  Used for sea level pressure calculation, see http://bit.ly/1qYzlE6
const byte TRANSMITTER_STATION_ID = 1;      // Davis ISS station ID to be monitored.  Default station ID is normally 1

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

// char g_server [] = "weatherstation.wunderground.com";  // old WU server, still works, but use rtupdate instead
char g_server[] = "rtupdate.wunderground.com";  // Realtime update server - RapidFire
char g_API_server[] =  "api.wunderground.com";  // API server used to get WU data

// NTP Time Servers 
char g_timeServer1[] = "time.nist.gov";
char g_timeServer2[] = "utcnist.colorado.edu";



// Global variables used to store weather data that's sent to Weather Underground
byte     g_rainCounter =        0;  // rain data sent from outside weather station.  1 = 0.01".  Just counts up to 127 then rolls over to zero
byte     g_windgustmph =        0;  // Wind in MPH
float    g_dewpoint =         0.0;  // Dewpoint F
uint16_t g_dayRain =            0;  // Accumulated rain for the day in 1/100 inch
float    g_rainRate =           0;  // Rain rate in inches per hour
float    g_insideTemperature =  0;  // Inside temperature in tenths of degrees
int16_t  g_outsideTemperature = 0;  // Outside temperature in tenths of degrees
uint16_t g_barometer =          0;  // Current barometer in inches mercury * 1000
byte     g_outsideHumidity =    0;  // Outside relative humidity in %.
byte     g_windSpeed =          0;  // Wind speed in miles per hour
float    g_windDirection_Now =  0;  // Instantanious wind direction, from 1 to 360 degrees (0 = no wind data)
uint16_t g_windDirection_Avg =  0;  // Average wind direction, from 1 to 360 degrees (0 = no wind data)
bool     g_gotInitialWeatherData = false;  // flag to indicate when weather data is first received.  Used to prevent zeros from being uploaded to Weather Underground upon startup
bool     g_isBatteryOk = true;    // Battery status in ISS transmitter 

// I/O pins
const byte RX_OK =    A0;  // LED flashes green every time Moteino receives a good packet
const byte RX_BAD =   A1;  // LED flashes red every time Moteinoo receives a bad packet
const byte TX_OK =     3;  // LED flashed green when data is sucessfully uploaed to Weather Underground

const uint16_t UPLOAD_FREQ_SEC = 60; // Upload every 60 seconds

#if defined(__AVR_ATmega1284P__)
  // Moteino Mega
  // Etherent setup
  byte g_mac[] = { 0x90, 0xA2, 0xDA, 0x00, 0xFD, 0x18 }; // Moteino mega Ethernet board used for testing
  byte g_ip[] =  { 192, 168, 46, 85 };  // Vermont
  // byte g_ip[] =  { 192, 168, 130, 50 };  // Melrose

  // Moteino Mega I/O
  const byte MOTEINO_LED =      15;  // Moteino MEGA has LED on D15
  const byte SS_PIN_RADIO =      4;  // Slave select for Radio
  const byte SS_PIN_ETHERNET =  12;  // Slave select for Ethernet module
  const byte SS_PIN_MICROSD =   13;  // SS pin for micro SD card
  
#elif defined(__AVR_ATmega328P__)
  // Standard Moteino
  // Ethernet setup
  byte g_mac[] = { 0xDE, 0xAD, 0xBD, 0xAA, 0xAB, 0xA4 }; // Moteino Ethernet board
  byte g_ip[] =  { 192, 168, 46, 86 };  // Vermont
  // byte g_ip[] =  { 192, 168, 130, 51 };  // Melrose

  // Standard Moteino I/O
  const byte MOTEINO_LED =       9;  // Moteino has LED on D9
  const byte SS_PIN_RADIO =     10;  // Slave select for Radio
  const byte SS_PIN_ETHERNET =   7;  // Slave select for Ethernet module
#endif

// Used to track first couple of rain readings so initial rain counter can be set
enum rainReading_t { NO_READING, FIRST_READING, AFTER_FIRST_READING };
rainReading_t g_initialRainReading = NO_READING;  // Need to know when very first rain counter reading comes so inital calculation is correct  

enum timezone_t { UTCZONE, ESTZONE };

// Instantiate class objects
EthernetClient client;
EthernetUDP Udp;       // UDP for the time server
DavisRFM69 radio;
RTC_Millis rtc;        // software clock
Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(10085); // Barometric pressure sensor, uses I2C. http://www.adafruit.com/products/1603

// Function Prototypes
bool   getWirelessData();
void   decodePacket();
bool   getTodayRain();
bool   uploadWeatherData();
void   updateRainAccum();
void   updateRainRate(uint16_t rainSeconds);
void   avgWindDir(float windDir);
bool   updateBaromoter();
bool   updateInsideTemp();
float  dewPointFast(float tempF, byte humidity);
float  dewPoint(float tempf, byte humidity);
float  windChill(float tempDegF, float windSpeedMPG);
void   printFreeRam();
void   printData();
void   printRadioInfo();
void   printIssPacket();  
void   blink(byte PIN, int DELAY_MS);
bool   isNewDay();
void getFormattedTime(char timebuf[], timezone_t timezone);
bool refreshNtpTime();
time_t getNewNtpTime();
time_t decodeNtpTime(char* address);
void   sendNTPpacket(char* address, byte *packetBuff, int PACKET_SIZE);
void   softReset();


//=============================================================================
//=============================================================================
void setup() 
{
  wdt_disable();
  Serial.begin(9600);
  delay(6000); 
  
  // Read and update reboot counter in EEPROM memory
  const uint8_t ADDR_REBOOT = 2;      // EEPROM address for reboot counter
  uint16_t      Reboot_Counter;       // Counts reboots - integer
  uint8_t       Reboot_Counter_b[2];  // reboot counter converted to two bytes
 
  // Read two bytes that make up reboot counter
  Reboot_Counter_b[0] = EEPROM.read(ADDR_REBOOT);
  Reboot_Counter_b[1] = EEPROM.read(ADDR_REBOOT+1);
  
  // Convert bytes to an integer
  Reboot_Counter  = Reboot_Counter_b[0] << 8;
  Reboot_Counter |= Reboot_Counter_b[1];
  
  // Increment reboot counter
  Reboot_Counter++;

  // Convert reboot counter back to two bytes
  Reboot_Counter_b[0] = Reboot_Counter >> 8 & 0xff;
  Reboot_Counter_b[1] = Reboot_Counter & 0xff;
  
  // Save new reboot counter to eeprom
  EEPROM.write(ADDR_REBOOT,   Reboot_Counter_b[0]);
  EEPROM.write(ADDR_REBOOT+1, Reboot_Counter_b[1]);
  
  pinMode(MOTEINO_LED, OUTPUT);
  pinMode(RX_OK,       OUTPUT);
  pinMode(RX_BAD,      OUTPUT);
  pinMode(TX_OK,       OUTPUT);

  // Enable internal pull-up resistors for SPI CS pins, ref: http://www.dorkbotpdx.org/blog/paul/better_spi_bus_design_in_3_steps
  pinMode(SS_PIN_ETHERNET, OUTPUT);
  pinMode(SS_PIN_RADIO,  OUTPUT);
  digitalWrite(SS_PIN_ETHERNET, HIGH);
  digitalWrite(SS_PIN_RADIO,  HIGH);
  delay(1);

  // Flash all LEDs to indicate startup
  for (int i = 0; i < 10; i++)
  {
    digitalWrite(MOTEINO_LED, !digitalRead(MOTEINO_LED));
    digitalWrite(RX_OK,       !digitalRead(RX_OK));
    digitalWrite(RX_BAD,      !digitalRead(RX_BAD));
    digitalWrite(TX_OK,       !digitalRead(TX_OK));
    delay(150);
  }
      
  // Setup Ethernet
  W5100.select(SS_PIN_ETHERNET);  // Set slave select pin - requires modified w5100.h/cc 
   
  unsigned int localPort = 8888;     // Local port to listen for UDP packets
  Ethernet.begin(g_mac, g_ip);  
  Udp.begin(localPort);  // local port 8888 to listen for UDP packet for NTP time server
  delay(2000); 
  
  #ifdef PRINT_SETUP_INFO
    Serial.print(F("Weather Station "));
    Serial.println(VERSION);
    Serial.print(F("Reboots = "));
    Serial.println(Reboot_Counter);
    Serial.println(Ethernet.localIP());
  #endif
  
  // Get time from NTP time server and start real time clock
  time_t t = getNewNtpTime();  
  if ( t != 0 )
  { rtc.begin(DateTime(t)); }

  // Read the current daily rain accumulation from Weather Underground so program has the right starting point
  for (byte iGetRain = 0; iGetRain < 10; iGetRain++)
  { if ( !getTodayRain() )
    { delay(3000);} // didn't get rain data, wait then try again
    else
    { break; } // got rain data, exit the for loop
  }

  // Set up BMP085 pressure and temperature sensor
  if (!bmp.begin())
  { 
    #ifdef PRINT_DEBUG
      Serial.println(F("Could not find BMP180")); 
    #endif
  }

  // Setup Moteino radio
  radio.initialize();
  radio.setChannel(0); // Frequency - Channel is *not* set in the initialization. Need to do it now

  #ifdef PRINT_SETUP_INFO
    printFreeRam();
    Serial.println();
  #endif
Serial.println(F("WDTTEST 1")); //srg12 debug, thinking of turning WDT timer on here  wdt_enable(WDTO_8S);
}  // end setup()


//=============================================================================
//=============================================================================
void loop()
{
  // Heartbeat LED
  static uint32_t heartBeatLedTimer = millis(); // Used to flash heartbeat LED
  if ( (long)(millis() - heartBeatLedTimer) > 200 )
  {
    digitalWrite(MOTEINO_LED, !digitalRead(MOTEINO_LED));
    heartBeatLedTimer = millis();
  }
  
  refreshNtpTime();  // refreshes NTP time every 100 minutes
  DateTime now = rtc.now();

  // Get wireless weather station data 
  bool haveFreshWeatherData = getWirelessData();
   
  // Send data to Weather Underground PWS.  Normally takes about 700mS
  static uint32_t uploadTimer = 0; // timer to send data to Weather Underground
  static uint32_t lastUploadTime = millis();  // Timestamp of last upload to Weather Underground.  Use for reboot if no WU uploads in a while.
  bool isTimeToUpload = (long)(millis() - uploadTimer) > 0;
  if( isTimeToUpload && haveFreshWeatherData && g_gotInitialWeatherData )
  {
    // Get data that's not from outside weather station - baramoter and inside temp
    g_dewpoint = dewPoint( (float)g_outsideTemperature/10.0, g_outsideHumidity);
    updateBaromoter();  
    updateInsideTemp();
    
    wdt_enable(WDTO_8S); // Turn watchdog on - only want it running for Ethernet upload.  Can't use it for radio because it would reset when frequency hopping
    Serial.print(F("WDT On "));
    Serial.println("1");
    if( uploadWeatherData() )       // Upload weather data
    { lastUploadTime = millis(); }  // If upload was successful, save timestamp
    wdt_disable(); 
    Serial.print(F("WDT Off "));
    Serial.println("2");
    
    uploadTimer = millis() + (UPLOAD_FREQ_SEC * 1000); // set timer to upload again in UPLOAD_FREQ_SEC seconds
  }
  else if ( isTimeToUpload && haveFreshWeatherData )
  {
    // sketch is not getting new data from radio, send message to serial monitor
    #ifdef PRINT_DEBUG
      char formatedDate[25];
      getFormattedTime(formatedDate, ESTZONE); 
      Serial.print(F("No new data from ISS, "));
      Serial.println(formatedDate); 
   #endif
  }
  
  // Reboot if millis is close to rollover. RTC_Millis won't work properly if millis rolls over.  Takes 49 days to rollover
  if( millis() > 4294000000UL )
  { softReset(); }

  // Reboot if no Weather Underground uploads in 30 minutes (1,800,000 mS) and  program is getting data from radio
  // If device isn't getting data from Davis ISS, then it's not supposed to upload to Weather Underground
  if( ((long)( millis() - lastUploadTime ) > 1800000L ) && haveFreshWeatherData )
  { 
    Serial.println(F("Reboot - No WU Upload"));
    delay(100);  // time to send text to serial monitor
    softReset(); 
  }
  
}  // end loop()


//=============================================================================
// Read wireless data coming from Davis ISS weather station
//=============================================================================
bool getWirelessData()
{
  static uint32_t lastRxTime = 0;  // timestamp of last received data.  Doesn't have to be good data
  static byte hopCount = 0;

  bool gotGoodData = false;  // Initialize
  
  // Process wireless ISS packet
  if ( radio.receiveDone() )
  {
    packetStats.packetsReceived++;
    // check CRC
    unsigned int crc = radio.crc16_ccitt(radio.DATA, 6);
    if ((crc == (word(radio.DATA[6], radio.DATA[7]))) && (crc != 0))
    {
      decodePacket();  
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
      Serial.println(F("---Got CRC error")); // srg12 temp debug
    }
    
    // Weather CRC is right or not, we count that as reception and hop
    lastRxTime = micros()/1000UL;
    radio.hop();
  } // end if(radio.receiveDone())
  
  // If a packet was not received at the expected time, hop the radio anyway
  // in an attempt to keep up. Give up after 25 failed attempts. Keep track
  // of packet stats as we go. I consider a consecutive string of missed
  // packets to be a single resync. Thanks to Kobuki for this algorithm.
  // Formula for packet interval = (40 + STATION ID)/16 seconds.  ID is Davis ISS ID, default is 1 (which is 0 in packet data)
  // See: http://www.wxforum.net/index.php?topic=24981.msg240797#msg240797 
  // Dekay uses 2555 for Packet Interval, see: http://git.io/vqDqS
  const uint16_t PACKET_INTERVAL = (40.0 + TRANSMITTER_STATION_ID)/16.0 * 1000.0; 
  if ( (hopCount > 0) && (( (micros()/1000UL) - lastRxTime) > (hopCount * PACKET_INTERVAL + 200)) )
  {
    packetStats.packetsMissed++;
    if (hopCount == 1)
    { packetStats.numResyncs++; }
    
    if (++hopCount > 25)
    { 
      hopCount = 0; 
      Serial.println(F("---hopCount > 25")); // srg12 temporary debug
    }
    radio.hop();
  }
  
  return gotGoodData;
  
} // end getWirelessData()


//=============================================================================
// Received data and it passed CRC check.  This function decodes the weather data 
// Every packet contains wind speed, wind direction and battery status
// Packets take turns reporting other data
//=============================================================================
void decodePacket() 
{
  const byte SENSOR_OFFLINE = 0xFF;

  // Flags are set true as each variable comes in for the first time
  static bool gotTempData =     false;
  static bool gotHumidityData = false; 
  static bool gotRainData =     false;
  float       uvi =               0.0;  // UV index
  float       sol =               0.0;  // Solar radiatioin
  uint16_t    tt =                  0;  // Dummy variable for calculations
  uint16_t    rainSeconds =         0;  // seconds between rain bucket tips
  byte        byte4MSN =            0;  // Holds MSB of byte 4 - used for seconds between bucket tips

  // station ID - the low order three bits are the station ID.  Station ID 1 on Davis is 0 in this data
  byte stationId = (radio.DATA[0] & 0x07) + 1;
  if ( stationId != TRANSMITTER_STATION_ID )
  { return; }  // exit function if this isn't the station ID program is monitoring

  // Every packet has wind speed, wind direction and battery status in it
  g_windSpeed = radio.DATA[1];  

  // There is a dead zone on the wind vane. No values are reported between 8
  // and 352 degrees inclusive. These values correspond to received byte
  // values of 1 and 255 respectively
  // See http://www.wxforum.net/index.php?topic=21967.50
  //  float windDir = 9 + radio.DATA[2] * 342.0f / 255.0f; - formula has dead zone from 352 to 10 degrees
  if ( radio.DATA[2] == 0 )
  { g_windDirection_Now = 0; }
  else 
  { g_windDirection_Now = ((float)radio.DATA[2] * 1.40625) + 0.3; }  // This formula doesn't have dead zone, see: http://bit.ly/1uxc9sf
  avgWindDir(g_windDirection_Now);  // Average out the wind direction with vector math

  // 0 = battery ok, 1 = battery low.  
  if ( ((radio.DATA[0] & 0x8) >> 3) == 0 )
  { g_isBatteryOk = true; }
  else
  { g_isBatteryOk = false; }
  
  // Look at MSB in firt byte to get data type
  switch (radio.DATA[0] >> 4)
  {
  case ISS_OUTSIDE_TEMP:
    g_outsideTemperature = (int16_t)(word(radio.DATA[3], radio.DATA[4])) >> 4;
    gotTempData = true;  // one-time flag when data first arrives.  Used to track when all the data has arrived and can be sent to PWS  
    break;

  case ISS_HUMIDITY:
    // Round humidity to nearest integer
    g_outsideHumidity = (byte) ( (float)( word((radio.DATA[4] >> 4), radio.DATA[3]) ) / 10.0 + 0.5 );
    gotHumidityData = true;  // one-time flag when data first arrives
    break;
  
  case ISS_WIND_GUST:
    g_windgustmph = radio.DATA[3];
    break;
      
  case ISS_RAIN: 
    g_rainCounter = radio.DATA[3];
    if ( g_initialRainReading == NO_READING )
    { g_initialRainReading = FIRST_READING; } // got first rain reading
    else if ( g_initialRainReading == FIRST_READING )
    { g_initialRainReading = AFTER_FIRST_READING; } // already had first reading, now it's everything after the first time
    gotRainData = true;   // one-time flag when data first arrives

    updateRainAccum();
    break;

  case ISS_RAIN_SECONDS:  // Seconds between bucket tips, used to calculate rain rate.  See: http://www.wxforum.net/index.php?topic=10739.msg190549#msg190549
    byte4MSN = radio.DATA[4] >> 4;
    if ( byte4MSN < 4 )
    { rainSeconds =  (radio.DATA[3] >> 4) + radio.DATA[4] - 1; }  
    else
    { rainSeconds = radio.DATA[3] + (byte4MSN - 4) * 256; }   

    updateRainRate(rainSeconds);
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
    break;

  default:
    break;
  }
  
  // See if all weather data has been received
  // If so, program can start uploading to Weather Underground PWS
  if ( gotTempData && gotHumidityData && gotRainData )
  { g_gotInitialWeatherData = true; }
  
  #ifdef PRINT_WEATHER_DATA
    printData();  // Print weather data, useful for debuggging what's getting sent to WU
  #endif
  
} //  end decodePacket()


//=============================================================================
// Gets the daily rain accumulation from weather underground
// This is needed after a reboot because the Arduino doesn't retain it
//=============================================================================
bool getTodayRain()
{
  TextFinder finder( client );
  float rainToday = 0.0;
  #ifdef PRINT_DEBUG
    Serial.println(F("Get rain data")); 
  #endif
  if (client.connect(g_API_server, 80))
  {
    #ifdef PRINT_DEBUG
      Serial.println(F("Connected to WU API server"));  
    #endif
    // Make a HTTP request:
    // other station client.println(F("GET /api/cb0578a32efb0c50/conditions/q/pws:KVTREADS4.json HTTP/1.1"));
    client.println(F("GET /api/cb0578a32efb0c50/conditions/q/pws:KVTWESTD3.json HTTP/1.1"));
    client.println(F("Host: api.wunderground.com"));
    client.println(F("Connection: close"));
    client.println();
  }
  
  if (client.connected())
  {
    if(finder.find("precip_today_in"))
    {
      g_dayRain = finder.getFloat() * 100.0; // convert to 1/100 inch
      if (g_dayRain > 1000)  // srg debug - 2/3/17 finder.getFlaot is returning big values, don't know why
      { 
        g_dayRain  = 0;
        #ifdef PRINT_DEBUG
          Serial.println(F("Invalid rain data from WU, setting to 0")); 
        #endif
        client.stop();
        return false;
      }  
      #ifdef PRINT_DEBUG
        Serial.print(F("Daily Rain Start = "));
        Serial.println(g_dayRain);
      #endif
      client.stop();
      return true;
    }
    else  // Textfinder didn't find precip_today_in string json repsonse
    {
      #ifdef PRINT_DEBUG
        Serial.println(F("TextFinder failed"));
      #endif
      client.stop();
      return false;
    }
  }  
  
  // Client connection failed
  #ifdef PRINT_DEBUG
    Serial.println(F("Did not get rain data")); 
  #endif  
  client.stop();
  return false;
  
}  // end getTodayRain()


//=============================================================================
// Upload to Weather Underground
//=============================================================================
bool uploadWeatherData()
{
  uint32_t uploadApiTimer = millis();  // Used to time how long it takes to upload to WU

  
  wdt_reset();
  Serial.print(F("WDT Rst "));
  Serial.println("3");

  // Send the Data to weather underground
  if (client.connect(g_server, 80))
  {
    #ifdef PRINT_DEBUG_WU_UPLOAD
      Serial.print(F("Connected to: "));
      Serial.println(g_server);  
    #endif
    
    client.print("GET /weatherstation/updateweatherstation.php?ID=");
    client.print(WUNDERGROUND_STATION_ID);
    client.print("&PASSWORD=");
    client.print(WUNDERGROUND_PWD);
    client.print("&dateutc=now");
    client.print("&winddir=");
    client.print(g_windDirection_Avg);
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
    client.print("&realtime=1&rtfreq=");  // Need when uploading to RapidFire server
    client.print(UPLOAD_FREQ_SEC);        // Tell Rapidfire how often data will be uploaded (seconds)
    client.println("/ HTTP/1.1\r\nHost: host:port\r\nConnection: close\r\n\r\n"); // See forum post https://forum.arduino.cc/index.php?topic=461649.msg3199335#msg3199335
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
  wdt_reset();
  Serial.print(F("WDT Rst "));
  Serial.println("4");

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
        Serial.print(F("\n\nEthernet Timeout. Waited "));
        Serial.print((long)(millis() - uploadApiTimer));
        Serial.println(F(" mS"));
      #endif
      return false;
    }    
  }  // end while (client.connected() )
  
  wdt_reset();
  Serial.print(F("WDT Rst "));
  Serial.println("5");
  
  client.stop();
  blink(TX_OK, 50);
  #ifdef PRINT_DEBUG_WU_UPLOAD
    if ( (long)(millis() - uploadApiTimer) > 1500 )
    {
      Serial.print(F("WU upload took (mS): "));
      Serial.println((long)(millis() - uploadApiTimer));
    }
  #endif
  
  // Print EST time
  #ifdef PRINT_DEBUG_WU_UPLOAD
    // Get UTC time and format
    char formatedDate[25];
    getFormattedTime(formatedDate, ESTZONE); 
    Serial.println(formatedDate); 
    Serial.println();
  #endif
  
  wdt_reset();
  Serial.print(F("WDT Rst "));
  Serial.println("6");

  return true;
  
} // end uploadWeatherData()


//=============================================================================
// Track daily rain accumulation.  Reset at midnight (local time, not UDT time)
// Everything is in 0.01" units of rain
//=============================================================================
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


//=============================================================================
// Calculate rain rate in inches/hour
// rainSeconds seconds is sent from ISS.  It's the number fo seconds since the last bucket tip
//=============================================================================
void updateRainRate( uint16_t rainSeconds )
{
  if ( rainSeconds < 1020 )
  { g_rainRate = 36.0 / (float)rainSeconds; }
  else
  { g_rainRate = 0.0; }  // More then 15 minutes since last bucket tip, can't calculate rain rate until next bucket tip
  
}  // end updateRainRate()


//=============================================================================
// Calculate average of wind direction
// Because wind direction goes from 359 to 0, use vector averaging to determine direction
//=============================================================================
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

  g_windDirection_Avg = (int)avgWindDir % 360; // atan2() result can be > 360, so use modulus to just return remainder
  
  if (g_windDirection_Avg >=1 )
  { g_windDirection_Avg = g_windDirection_Avg - 1; } // convert range back to 0-359
   
}  // end avgWindDir()


//=============================================================================
// Get pressure reading from baromoter - located inside, not outside on weather station
// Barometric pressure is read once per minute
// Units from the BMP180 are Pa.  Need to convert to thousandths of inches of Hg.
//=============================================================================
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


//=============================================================================
// Inside temperature from BMP180 sensor
//=============================================================================
bool updateInsideTemp()
{
  bmp.getTemperature(&g_insideTemperature);
  g_insideTemperature = g_insideTemperature * 1.8 + 32.0;  // convert to F 
  if (g_insideTemperature > 35 && g_insideTemperature < 110) // Validate indoor temperature range
  {  return true; }
  else
  { return false; }
} // end updateInsideTemp()


//=============================================================================
// Calculate the dew point, this is supposed to be the fast version
// Ref: http://playground.arduino.cc/Main/DHT11Lib
//=============================================================================
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


//=============================================================================
// Another dewpoint calculation - slower
//=============================================================================
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


//=============================================================================
// Wind Chill = 35.74 + 0.6215T - 35.75(V^0.16)+0.4275T(V^0.16)
// Source: http://www.crh.noaa.gov/ddc/?n=windchill
// V is wind speed in MPH
// T is degrees F
//=============================================================================
float windChill(float tempDegF, float windSpeedMPG)
{
  
  float v2 = pow(windSpeedMPG, 0.16);   // http://arduino.cc/en/Reference/Pow
  float windChillTempF = 35.74 + 0.6215 * tempDegF - 35.75 * v2 + (0.4275 * tempDegF) * v2;
  return windChillTempF;
  
} // end windChill()


//=============================================================================
// Return true if it's a new day. This is local (EST) time, not UTC
// Used to reset daily rain accumulation
//=============================================================================
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


//=============================================================================
//=============================================================================
void printFreeRam() 
{
  extern int __heap_start, *__brkval;
  int v;
  Serial.print(F("Free mem: "));
  Serial.println((int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval));
} // end printFreeRam()


//=============================================================================
// Prints data - used for debugging
//=============================================================================
void printData()
{
#ifdef PRINT_WEATHER_DATA
  static uint32_t timeElapsed = millis(); // time elapsed since last time printData() was called.  Pretty much the same as time between data packets received
  static byte headerCounter = 0;
  
  // Header
  if (headerCounter == 0)
//  { Serial.println(F("RxTime\tR-Cnt\tdaily\tr_secs\tr-rate\tHrs\tspeed\tgusts\tAvgDir\tNowDir\ttemp\thumid\tbat\tCRC\tpacket")); }
  { Serial.println(F("RxTime\tdaily\tr-rate\tHrs\tspeed\tgusts\tAvgDir\tNowDir\ttemp\thumid\tbat\tCRC")); }
  
  if( headerCounter++ > 20)
  { headerCounter = 0; }
  
  Serial.print(millis() - timeElapsed);
  Serial.print(F("\t"));
  Serial.print(g_dayRain);
  Serial.print(F("\t"));
  Serial.print(g_rainRate);
  Serial.print(F("\t"));
  Serial.print(millis()/3600000.0);
  Serial.print(F("\t"));
  Serial.print(g_windSpeed);
  Serial.print(F("\t"));
  Serial.print(g_windgustmph);
  Serial.print(F("\t"));
  Serial.print(g_windDirection_Now);
  Serial.print(F("\t"));
  Serial.print(g_windDirection_Avg);
  Serial.print(F("\t"));
  Serial.print(g_outsideTemperature);
  Serial.print(F("\t"));
  Serial.print(g_outsideHumidity);
  Serial.print(F("\t"));
  Serial.print(g_isBatteryOk);
  Serial.print(F("\t"));
  Serial.print( packetStats.crcErrors);
  Serial.print(F("\t"));
//  printIssPacket();
  Serial.println();
  
  timeElapsed = millis(); // save new timestamp
#endif 
}  // end printData()
 

//=============================================================================
// Prints info about the radio 
//=============================================================================
void printRadioInfo()
{ 
  // Print radio info
  if ( g_initialRainReading == FIRST_READING)
  {
    Serial.print(F("ISS ID "));
    Serial.print((radio.DATA[0] & 0x07) + 1);
    Serial.print(F("\tch: "));
    Serial.print(radio.CHANNEL);
    Serial.print(F("\tRSSI: "));
    Serial.println(radio.RSSI);
  } 
}  // end printRadioInfo()


//=============================================================================
// print ISS data packet
//=============================================================================
void printIssPacket()
{
  for (byte i = 0; i < DAVIS_PACKET_LEN; i++)
  {
    if( radio.DATA[i] < 16 )
    { Serial.print(F("0"));}  // leading zero
    Serial.print(radio.DATA[i], HEX);
    Serial.print(F(" "));
  }
} // end printIssPacket()


//=============================================================================
// Format GMT Time the way weather underground wants
// dateutc - [YYYY-MM-DD HH:MM:SS (mysql format)]
// Time is 23 characters long, 
// Data need to be URL escaped: 2014-01-01 10:32:35 becomes 2014-01-01+10%3A32%3A35
// Every 10 minutes sketch will query NTP time server to update the time
//=============================================================================
void getFormattedTime(char timebuf[], timezone_t timezone)
{
  // Get time and format either UTC or EST timezone
  DateTime now = rtc.now();
  DateTime estNow = rtc.now() + (3600UL * -5L);  // Winter EST time zone

  switch (timezone)
  {
   case UTCZONE:
     sprintf(timebuf, "%d-%02d-%02d+%02d%%3A%02d%%3A%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
     break;
   case ESTZONE:
     sprintf(timebuf, "%d:%02d:%02d  %d/%d/%d", estNow.hour(), estNow.minute(), estNow.second(), estNow.month(), estNow.day(),estNow.year() );
     break;
   default:  // dafault to UTC
     sprintf(timebuf, "%d-%02d-%02d+%02d%%3A%02d%%3A%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
     break;
  }
  
} // end getFormattedTime()


//=============================================================================
//  Refresh the NTP time once in a while
//=============================================================================
bool refreshNtpTime()
{                               
  const uint32_t REFRESH_DELAY = 6000000;  // Get NTP time again in 100 minutes
  static uint32_t refreshNTPTimeTimer = millis() + REFRESH_DELAY; // Timer used to determine when to get time from NTP time server
   
  if( (long)(millis() - refreshNTPTimeTimer) > 0 )
  {
    refreshNTPTimeTimer = millis() + REFRESH_DELAY;  // reset timer

    // Update NTP time
    time_t t = getNewNtpTime();
    if ( t != 0 )
    { 
      rtc.adjust(DateTime(t)); 
      return true;
    }
    else
    { return false; }
  }
  else  // Not time to refresh NTP time
  { return false; }
  
} // end refreshNtpTime()
  

//=============================================================================
//  Connect to timeserver
//  time_t is epoch time format
// Dec 23, 2017 11:30 AM =  1514046516
//=============================================================================
time_t getNewNtpTime()
{

 time_t epochTime = 0;

    // Looop several times to get NTP time
    for (byte cnt = 0; cnt < 10; cnt++)
    {
      epochTime = decodeNtpTime(g_timeServer1);
      if ( epochTime != 0 )
      { 
        #ifdef PRINT_DEBUG
          Serial.print(F("Got NTP time - "));
          Serial.println(g_timeServer1);
        #endif
        return epochTime;  // exits sub 
      }
      else // didn't get time.  Try other server
      { 
        #ifdef PRINT_DEBUG
          Serial.print(F("Didn't get NTP time - "));
          Serial.println(g_timeServer1);
        #endif
        delay(2000);
        epochTime = decodeNtpTime(g_timeServer2);
        if ( epochTime != 0 )
        { 
          #ifdef PRINT_DEBUG
            Serial.print(F("Got NTP time - "));
            Serial.println(g_timeServer2);
          #endif
          return epochTime;  // exits sub 
        }
        else // didn't get from 2nd server
        {
          #ifdef PRINT_DEBUG
            Serial.print(F("Didn't get NTP time - "));
            Serial.println(g_timeServer1);
          #endif
          delay(2000);
        }
      }
    } // end for loop
 
   return 0;  // Didn't get time
 
} // end getNewNtpTime()


//=============================================================================
// Decodes time from NTP server
//=============================================================================
time_t decodeNtpTime(char* address)
{
  const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
  byte packetBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming & outgoing packets
  
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  { sendNTPpacket(address, packetBuffer, NTP_PACKET_SIZE); }
  
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
}  // end decodeNtpTime()


//=============================================================================
// send an NTP request to the time server at the given address
//=============================================================================
void sendNTPpacket(char* address, byte *packetBuff, int packetSize)  
{
  // set all bytes in the buffer to 0
  memset(packetBuff, 0, packetSize);
  // Initialize values needed to form NTP request
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
  Udp.beginPacket(address, 123); // NTP requests are to port 123
  Udp.write(packetBuff, packetSize);
  Udp.endPacket();
} // end sendNTPpacket()


//=============================================================================
// Blink LED
//=============================================================================
void blink(byte pin, int DELAY_MS)
{
  digitalWrite(pin, HIGH);
  delay(DELAY_MS);
  digitalWrite(pin, LOW);
} // end blink()


//=============================================================================
// Reboot
//=============================================================================
void softReset()
{
  asm volatile("  jmp 0");
}  // end softReset()





