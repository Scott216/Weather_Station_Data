/*
Testing ability to upload to Weather Underground.  

*/

#define VERSION "v0.04" // Version of this program

#include <Ethernet.h>          // Modified for user selectable SS pin and disables interrupts  
#include <SPI.h>               // DavisRFM69.h needs this   http://arduino.cc/en/Reference/SPI
#include "Tokens.h"            // Holds Weather Underground password


#define WUNDERGROUND_STATION_ID "KVTDOVER3" // Weather Underground station ID - test station

// Reduce number of bogus compiler warnings, see http://bit.ly/1pU6XMe
#undef PROGMEM
#define PROGMEM __attribute__(( section(".progmem.data") ))

char g_server [] = "weatherstation.wunderground.com";  // standard weather underground server for upload

// Etherent setup
byte g_mac[] = { 0xDE, 0xAD, 0xBD, 0xAA, 0xAB, 0xA4 };
byte g_ip[] = { 192, 168, 46, 85 };   // Static IP on LAN

// Dummy weather data to send to Weather Underground
//byte     g_rainCounter =        0;  // rain data sent from outside weather station.  1 = 0.01".  Just counts up to 127 then rolls over to zero
byte     g_windgustmph =        5;  // Wind in MPH
float    g_dewpoint =         33.0;  // Dewpoint F
int16_t  g_outsideTemperature = 258;  // Outside temperature in tenths of degrees
uint16_t g_barometer =        29800;  // Current barometer in inches mercury * 1000
byte     g_outsideHumidity =    90;  // Outside relative humidity in %.
byte     g_windSpeed =         2;  // Wind speed in miles per hour
float    g_windDirection_Now =  200;  // Instantanious wind direction, from 1 to 360 degrees (0 = no wind data)
uint16_t g_windDirection_Avg =  216;  // Average wind direction, from 1 to 360 degrees (0 = no wind data)

#ifdef __AVR_ATmega1284P__
  const byte MOTEINO_LED =      15;  // Moteino MEGA has LED on D15
  const byte SS_PIN_RADIO =      4;  // Slave select for Radio
  const byte SS_PIN_ETHERNET =  12;  // Slave select for Ethernet module
#else
  const byte MOTEINO_LED =       9;  // Moteino has LED on D9
  const byte SS_PIN_RADIO =     10;  // Slave select for Radio
  const byte SS_PIN_ETHERNET =   7;  // Slave select for Ethernet module
#endif

EthernetClient client;
   
// Function Prototypes
bool   uploadWeatherData();


//=============================================================================
//=============================================================================
void setup() 
{
  Serial.begin(9600);
  delay(6000); 

  Serial.print("WU Upload test.  Ver ");
  Serial.println(VERSION);

// Enable internal pull-up resistors for SPI CS pins, ref: http://www.dorkbotpdx.org/blog/paul/better_spi_bus_design_in_3_steps
  pinMode(SS_PIN_ETHERNET, OUTPUT);
  pinMode(SS_PIN_RADIO,  OUTPUT);
  digitalWrite(SS_PIN_ETHERNET, HIGH);
  digitalWrite(SS_PIN_RADIO,  HIGH);
  delay(1);

      
  // Setup Ethernet
  Ethernet.select(SS_PIN_ETHERNET);  // Set slave select pin - requires modified Ethernet.h/ccp and w5100.h/cpp files
  Ethernet.begin(g_mac, g_ip);  
  delay(2000); 

  
}  // end setup()


//=============================================================================
//=============================================================================
void loop()
{
  
  // Send data to Weather Underground PWS.  Normally takes about 700mS
  static uint32_t uploadTimer = 0; // timer to send data to Weather Underground
  static uint32_t lastUploadTime = millis();  // Timestamp of last upload to Weather Underground.  Use for reboot if no WU uploads in a while.
  bool isTimeToUpload = (long)(millis() - uploadTimer) > 0;

  if( isTimeToUpload )
  {
    if( uploadWeatherData() )       // Upload weather data
    { lastUploadTime = millis(); }  // If upload was successful, save timestamp
    uploadTimer = millis() + 30000; // set timer to upload again in 30 seconds
  }
 
   if (client.available())
   {
     char c = client.read();
     Serial.print(c);
   }
   if (!client.connected())
   {
 //    Serial.println("disconnecting...");
     client.stop();
   }
 
 
}  // end loop()



//=============================================================================
// Upload to Weather Underground
//=============================================================================
bool uploadWeatherData()
{
  uint32_t uploadApiTimer = millis();  // Used to time how long it takes to upload to WU

  
  // Send the Data to weather underground
  if (client.connect(g_server, 80))
  {
    client.print("GET /weatherstation/updateweatherstation.php?ID=");
    Serial.print(g_server);
    Serial.print("/weatherstation/updateweatherstation.php?ID=");
    client.print(WUNDERGROUND_STATION_ID);
    Serial.print(WUNDERGROUND_STATION_ID);
    client.print("&PASSWORD=");
    Serial.print("&PASSWORD=");
    client.print(WUNDERGROUND_PWD);
    Serial.print(WUNDERGROUND_PWD);
    client.print("&dateutc=now");
    Serial.print("&dateutc=now");
    client.print("&winddir=");
    Serial.print("&winddir=");
    client.print(g_windDirection_Avg);
    Serial.print(g_windDirection_Avg);
    client.print("&windspeedmph=");
    Serial.print("&windspeedmph=");
    client.print(g_windSpeed);
    Serial.print(g_windSpeed);
    client.print("&windgustmph=");
    Serial.print("&windgustmph=");
    client.print(g_windgustmph);
    Serial.print(g_windgustmph);
    client.print("&tempf=");
    Serial.print("&tempf=");
    client.print((float)g_outsideTemperature / 10.0);
    Serial.print((float)g_outsideTemperature / 10.0);
    client.print("&softwaretype=testing");
    Serial.print("&softwaretype=testing");
    client.print("&action=updateraw");
    Serial.print("&action=updateraw");
    client.println();
    Serial.println();
//    Serial.println(F("Uploaded to WU"));  //srg debug
  }
  else
  {
    Serial.println(F("\nWU connection failed"));
    client.stop();
    delay(500);
    return false;
  }
  
  /*
  uint32_t lastRead = millis();
  uint32_t connectLoopCounter = 0;  // used to timeout ethernet connection 
  
  while (client.connected() && (millis() - lastRead < 1000))  // wait up to one second for server response
//  while (client.connected() )  
  {
    while (client.available()) 
    {
      char c = client.read();
      connectLoopCounter = 0;  // reset loop counter
      Serial.print(c);
    }  
    
    connectLoopCounter++;
    // if more than 10000 loops since the last packet, then timeout
    if( connectLoopCounter > 10000L )
    {
      client.stop();
      Serial.print(F("\n\nEthernet Timeout. Waited "));
      Serial.print((long)(millis() - uploadApiTimer));
      Serial.println(F(" mS"));
      return false;
    }    
  }  // end while (client.connected() )
  
  
  client.stop();

    if ( (long)(millis() - uploadApiTimer) > 1500 )
    {
      Serial.print(F("WU upload took (mS): "));
      Serial.println((long)(millis() - uploadApiTimer));
    }
*/
  
  return true;
  
} // end uploadWeatherData()


