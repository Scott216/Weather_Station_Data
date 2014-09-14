Modified Ethernet files.  You need to replace original Ethernet with the ones in this directory.
Original files are located:

Windows:
C:\Program Files(x86)\Arduino\libraries\Ethernet\
C:\Program Files(x86)\Arduino\libraries\Ethernet\utility\ 

Mac:
/Applications/Arduino.app (show pkg contents) /Contents/Resources/Java/libraries/Ethernet/ 
/Applications/Arduino.app (show pkg contents) /Contents/Resources/Java/libraries/Ethernet/utility/ 

Slave select modification by SurferTim.  See Arduino forum post
http://forum.arduino.cc/index.php?topic=217423.msg1601862#msg1601862
Use as follows

#define SS_PIN 8
pinMode(SS_PIN, OUTPUT);
Ethernet.select(SS_PIN);
Ethernet.begin(mac, ip);


Info on disabling interrupts:
http://harizanov.com/2012/04/rfm12b-and-arduino-ethernet-with-wiznet5100-chip/