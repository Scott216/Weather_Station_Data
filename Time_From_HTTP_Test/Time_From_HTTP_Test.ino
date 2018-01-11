/*
   Extract the hour from HTTP GET response
 
 Typical response looks like this:
 HTTP/1.0 200 OK
 Content-type: text/html
 Date: Thu, 21 Dec 2017 17:08:54 GMT  
 Content-Length: 8
 
 All we want is to know when a new day start.  GMT is 5 hours ahead of EST (in the winter). so 5:00 is midnight
 In the reponse, look for a comma, then test the 14th and 15th characters
 So when char 14 = 0 and char 15 = 5, it's midnight

 
 
 */

#include <Ethernet.h>
#include <SPI.h>

byte mac[] = { 0x90, 0xA2, 0xDA, 0x00, 0xFD, 0x18 };
// byte ip[] = { 192, 168, 46, 85 };   // Vermont
byte ip[] = { 192, 168, 130, 50 };   // Melrose
//char server[] = "weatherstation.wunderground.com";
char server[] = "rtupdate.wunderground.com";   // Realtime update server - RapidFire
char WU_Data[] = "GET /weatherstation/updateweatherstation.php?ID=KVTDOVER3&PASSWORD=storm216&dateutc=now&winddir=165&windspeedmph=1&windgustmph=1&tempf=13.8&humidity=83&softwaretype=test2&action=updateraw&realtime=1&rtfreq=30";

EthernetClient client;

//================================================================
//================================================================
void setup()
{
  Ethernet.begin(mac, ip);
  Serial.begin(9600);

  delay(5000);
  
  Serial.println("Time from HTTP Test");
 
}

//================================================================
//================================================================
void loop()
{

  static unsigned long uploadTimer = millis();

  static int charCount = 0;
  static int commaPosition = 0;
  static byte hourA = 0;
  static byte hourB = 0;


  if (millis() - uploadTimer > 30000L  )
  { 
    uploadToWU();
    uploadTimer = millis();
  }

  
  if (client.available()) 
  {
    char c = client.read();
    Serial.print(c);
    charCount++;

    // Find position of the comma
    if ( strcmp(c, ',' ) == 0)
    { commaPosition = charCount; }

    // Get byte value of the first charcher of the hour
    if ( commaPosition > 0 && charCount == commaPosition + 14 )
    { hourA = c; }

    // Get byte value of the second charcher of the hour
    if ( commaPosition > 0 && charCount == commaPosition + 15 )
    { hourB = c; }

  } // end client.available()


  if (!client.connected() ) 
  {
    client.stop();
    charCount = 0;
    commaPosition = 0; 
  }


  // check for a new day
  if ( isNewDay(hourA, hourB) )
  {
    Serial.print("\n\n************** NEW DAY Horay!! *************\n\n");
  }  



} // end loop()

//================================================================
//================================================================
bool uploadToWU()
{

  Serial.println("try to upload data");
   
  if (client.connect(server, 80)) 
  {
    Serial.print(F("... Connected to server: "));
    Serial.print(server);
    char c = client.read();
    Serial.print(F(", Server response: "));
    Serial.write(c); 
    Serial.println(F(""));
    
    Serial.println(F("... Sending DATA "));
    Serial.println(F(""));
    
    client.print(WU_Data);
    client.println("/ HTTP/1.1\r\nHost: host:port\r\nConnection: close\r\n\r\n"); 
    return true;
  } 
  else 
  { 
    Serial.println("connection failed"); 
    return false;
  }
  
} // end uploadToWU()

//================================================================
//================================================================
// Midnight occurs when hourA = 48 and hourB goes from 52 to 53
bool isNewDay(byte newHourA, byte newHourB)
{
  static byte prevHourB;

  if (newHourA == 48 && newHourB == 53  && prevHourB == 52 )
  { 
    prevHourB = newHourB;
    return true;
  }
  else
  { 
    prevHourB = newHourB;
    return false;
  }
 
}




