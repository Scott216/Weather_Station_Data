#include <Ethernet.h>
#include <SPI.h>

byte mac[] = { 0xDE, 0xAD, 0xBD, 0xAA, 0xAB, 0xA4 };
byte ip[] = { 192, 168, 46, 85 }; 
char server [] = "google.com";
const byte SS_PIN_ETHERNET =   7;

EthernetClient client;

void setup()
{
  Ethernet.select(SS_PIN_ETHERNET);
  Ethernet.begin(mac, ip);
  Serial.begin(9600);

  delay(3000);
  
  Serial.println("connecting...");

  if (client.connect(server, 80)) 
  {
    Serial.println("connected to Google");
    client.println("GET /search?q=arduino HTTP/1.0");
    client.println();
  } 
  else 
  {
    Serial.println("connection failed");
  }
}

void loop()
{
  static int maxchar = 0;
  if (client.available() && maxchar <= 1000 ) 
  {
    char c = client.read();
    Serial.print(c);
    maxchar++;
  }

  if (!client.connected() || maxchar >= 1000) {
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();
    for(;;)
      ;
  }
}
