/*

Testing radio data only for Davis ISS with Moteino or MoteinoMega.  No decoding of the packet

Forum: http://bit.ly/14thCKB
GitHub Repo: http://github.com/LowPowerLab

Thread talking about Station IDs and frequency hopping
http://www.wxforum.net/index.php?topic=24981.msg240661#msg240661
http://www.wxforum.net/index.php?topic=24981.msg240797#msg240797


Posted sketch and data: http://pastebin.com/GPa0Fk9s


*/

#include <HardwareSerial.h>
#include <SPI.h>             // DavisRFM69.h needs this   http://arduino.cc/en/Reference/SPI
#include <DavisRFM69.h>      // http://github.com/dekay/DavisRFM69

// Reduce number of bogus compiler warnings, see http://bit.ly/1pU6XMe
#undef PROGMEM
#define PROGMEM __attribute__(( section(".progmem.data") ))


#ifdef __AVR_ATmega1284P__
  #define MOTEINO_LED          15 // Moteino MEGAs have LEDs on D15
  const byte SS_PIN_RADIO =     4;
  const byte SS_PIN_ETHERNET = 12; 
#else
  #define MOTEINO_LED           9 // Moteinos have LEDs on D9
  const byte SS_PIN_RADIO =    10;
  const byte SS_PIN_ETHERNET = 7; 
#endif


#define ISS_UV_INDEX     0x4
#define ISS_RAIN_SECONDS 0x5
#define ISS_SOLAR_RAD    0x6
#define ISS_OUTSIDE_TEMP 0x8
#define ISS_WIND_GUST    0x9
#define ISS_HUMIDITY     0xA
#define ISS_RAIN         0xE

DavisRFM69 radio;

const byte TRANSMITTER_STATION_ID = 1;      // ISS station ID to be monitored.  Default station ID is normally 1

byte g_BatteryStatus = 0;  // 0 = battery okay, 1 = battery low

// uint16_t g_packet_interval = 2554; // Packet delay
// uint16_t g_packet_interval = 3098; // Packet delay

//=============================================================================
// Setup radio
//=============================================================================
void setup()
{
  Serial.begin(9600);
  
  pinMode(MOTEINO_LED,   OUTPUT);
  digitalWrite(MOTEINO_LED, LOW);
  
  pinMode(SS_PIN_ETHERNET, OUTPUT);
  digitalWrite(SS_PIN_ETHERNET, HIGH);
  
  // Setup Moteino radio
  radio.initialize();
  radio.setChannel(0); // Frequency - Channel is *not* set in the initialization. Need to do it now
  delay(2000);
  
  if (SS_PIN_RADIO == 4 )
  { Serial.println("Davis ISS Radio test for MoteinoMega"); }
  else
  { Serial.println("Davis ISS readio test for normal Moteino"); }
  
}  // end setup()


//=============================================================================
// Main loop()
//=============================================================================
void loop()
{

  bool haveFreshWeatherData = getWirelessData();
  
  // Heartbeat LED
  static uint32_t heartBeatLedTimer = millis();
  if ( (long)(millis() - heartBeatLedTimer) > 200 )
  {
    digitalWrite(MOTEINO_LED, !digitalRead(MOTEINO_LED));
    heartBeatLedTimer = millis(); 
  }
  
} // end loop()


//=============================================================================
// Read wireless data coming from Davis ISS weather station
//=============================================================================
bool getWirelessData()
{
  static uint32_t lastRxTime = 0;  // timestamp of last received data.  Doesn't have to be good data
  static byte hopCount = 0;
  static uint32_t interval_incrementer_timer = millis(); // increments packet delay every 10 mintues
  static uint32_t lastCrcErrTime = 0;
  
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
      gotGoodData = true;
    }
    else
    {
      packetStats.crcErrors++;
      packetStats.receivedStreak = 0;
      Serial.print("Got CRC error. ");
      printPacket();
      Serial.print("   Seconds since last error: ");
      Serial.print((millis() - lastCrcErrTime)/1000);
      Serial.print("    Minutes running: ");
      Serial.println(millis()/60000.0);
      Serial.println();
      lastCrcErrTime = millis();
    }
    
    // Whether CRC is right or not, we count that as reception and hop
    lastRxTime = millis();
    radio.hop();
  } // end if(radio.receiveDone())
  
  // If a packet was not received at the expected time, hop the radio anyway
  // in an attempt to keep up.  Give up after 25 failed attempts.  Keep track
  // of packet stats as we go.  I consider a consecutive string of missed
  // packets to be a single resync.  Thx to Kobuki for this algorithm.
//  const uint16_t PACKET_INTERVAL = 2555;  // Default from Dekay
 const uint16_t PACKET_INTERVAL = (40.0 + TRANSMITTER_STATION_ID)/16.0 * 1000.0; 

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
//=============================================================================
void processPacket() 
{

  // Flags are set true as each variable comes in for the first time
  static bool gotTempData =     false;
  static bool gotHumidityData = false; 
  static bool gotRainData =     false;
  uint16_t    rainSeconds =         0;  // seconds between rain bucket tips
  byte        byte4MSN =            0;  // Holds MSB of byte 4 - used for seconds between bucket tips

  // station ID - the low order three bits are the station ID.  Station ID 1 is 0 in this data
//  byte stationId = (radio.DATA[0] & 0x07) + 1;
//srg  if ( stationId != TRANSMITTER_STATION_ID )
//srg  { return; }  // exit function if this isn't the station ID program is monitoring


  // 0 = battery ok, 1 = battery low.  Not used by anything in program
  g_BatteryStatus = (radio.DATA[0] & 0x8) >> 3; 
  
  printData();  // Print data, useful for debuggging
  
} //  end processPacket()


//=============================================================================
// Prints radio channel and RSSI
//=============================================================================
void printRadioInfo()
{
    Serial.print(F("ISS ID "));
    Serial.print((radio.DATA[0] & 0x07) + 1);
    Serial.print(F("\tch: "));
    Serial.print(radio.CHANNEL);
    Serial.print(F("\tRSSI: "));
    Serial.print(radio.RSSI);
    Serial.print("  ");

}  // end printRadioInfo()


//=============================================================================
// print ISS data packet in Hex
//=============================================================================
void printPacket()
{
  for (byte i = 0; i < DAVIS_PACKET_LEN; i++)
  {
    if( radio.DATA[i] < 16 )
    { Serial.print("0");}  // leading zero
    Serial.print(radio.DATA[i], HEX);
    Serial.print(" ");
  }
} // end printPacket()


//=============================================================================
// Prints ISS data - used for debugging
//=============================================================================
void printData()
{
  static uint32_t timeElapsed = millis(); // time elapsed since last tiem printData() was called.  Pretty much the same as time between data packets received
  static byte headerCounter = 0;

  // Header
  if (headerCounter == 0)
  { Serial.println("RxTime\tRSSI\tchan\tcrc err\tID\tpacket"); }
  
  if( headerCounter++ > 20)
  { headerCounter = 0; }
  
  Serial.print(millis() - timeElapsed);
  Serial.print("\t");
  Serial.print(radio.RSSI);
  Serial.print("\t");    
  Serial.print(radio.CHANNEL);
  Serial.print("\t");    
  Serial.print(packetStats.crcErrors);
  Serial.print("\t");    
  Serial.print((radio.DATA[0] & 0x07) + 1);
  Serial.print("\t");
  printPacket();
  Serial.println();
  
  timeElapsed = millis(); // save new timestamp
  
}  // end printData()



