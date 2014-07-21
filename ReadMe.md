<h2>************* STILL DEBUGGING *************</h2>

This project received Davis VantagePro wireless weather station data and sends to to Weather Underground's personal weather station (PWS).

Hardware:
[Moteino RFM69W 868/915 Mhz](https://lowpowerlab.com/shop/index.php?_route_=moteino-r4) (no Flash chip) (The Moteino basically an Arduino with a radio)
WIZ811MJ Wiznet Ethernet module, available from [Sparkfun](https://www.sparkfun.com/products/9473)
BMP180 Barometric Pressure sensor from [Adafruit](http://www.adafruit.com/products/1603)

All the hard work and decoding of the Davis wireless data was done by DeKay. He has a good [blog post](http://madscientistlabs.blogspot.com/2014/02/build-your-own-davis-weather-station_17.html) on what he did.  DeKay's goal was to grab the wireless weather station data and then send it to a weather station program on a PC.  This project bypasses all that and just sends it to Weather Underground's [Personal Weather Station network](http://www.wunderground.com/weatherstation/index.asp).

Because the Moteino uses slave select pin D10 for the radio, this pin can't be used with the Ethernet board.  The Arduino Ethernet.h library hard codes pin 10 as the slave select, so a modified library needs to be used.  The modification was done by [SurferTim](http://forum.arduino.cc/index.php?action=profile;u=49379) on the Arduino Forum.  There is a thread about the change [here](http://forum.arduino.cc/index.php?topic=217423.msg1601862#msg1601862). His files are in this Github repo and you'll need to replace Arduino's Ethernet.h/cpp and w5100.h/cpp files with these.  I haven't come across any problems with using these files. I'm currently on Arduino IDE 1.05



