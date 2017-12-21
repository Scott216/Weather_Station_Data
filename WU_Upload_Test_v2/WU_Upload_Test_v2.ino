/*

Arduino forum post with changeds to Weather Underground GET request format
https://forum.arduino.cc/index.php?topic=461649.0

*/

#include <Ethernet.h>
#include <SPI.h>

byte mac[] = { 0xDE, 0xAD, 0xBD, 0xAA, 0xAB, 0xA4 };
byte ip[] = { 192, 168, 46, 85 };   // Vermont
//char server[] = "weatherstation.wunderground.com";
char server[] = "rtupdate.wunderground.com";   // Realtime update server - RapidFire
char WU_Data[] = "GET /weatherstation/updateweatherstation.php?ID=KVTDOVER3&PASSWORD=storm216&dateutc=now&winddir=146&windspeedmph=4&windgustmph=8&tempf=20.00&humidity=50&softwaretype=test2&action=updateraw&realtime=1&rtfreq=60";

const byte SS_PIN_ETHERNET =   7;

EthernetClient client;

void setup()
{
  Ethernet.select(SS_PIN_ETHERNET);
  Ethernet.begin(mac, ip);
  Serial.begin(9600);

  delay(3000);
  

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
    client.println("/ HTTP/1.1\r\nHost: host:port\r\nConnection: close\r\n\r\n"); // Working since first days of March 2017
  
//    Serial.print(server);
    Serial.print(WU_Data);
    Serial.println();
  } 
  else 
  { Serial.println("connection failed"); }
}


void loop()
{
  if (client.available()) 
  {
    char c = client.read();
    Serial.print(c);
  }

  if (!client.connected() || millis() > 20000L ) 
  {
    Serial.println();
    Serial.println("disconnecting");
    client.stop();
    for(;;)
      ;
  }
}
