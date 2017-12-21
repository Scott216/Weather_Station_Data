// https://lowpowerlab.github.io/MoteinoCore/package_LowPowerLab_index.json


void setup() 
{
  Serial.begin(9600);
  delay(6000);
  
}

void loop() 
{
#if !defined(SPI_HAS_EXTENDED_CS_PIN_HANDLING)

  #if defined(ARDUINO_ARCH_AVR)
    Serial.println("ARDUINO_ARCH_AVR was defined");
    #if defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
      Serial.println("__AVR_ATmega1280__ was defined");
    #elif defined(__AVR_ATmega32U4__)
      Serial.println("__AVR_ATmega32U4__ was defined");
    #elif defined(__AVR_AT90USB1286__) || defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB162__)
      Serial.println("__AVR_AT90USBxxx was defined");
    #elif defined(__AVR_ATmega1284P__)   // srg added - guessed
      Serial.println("__AVR_ATmega1284__ was defined it's a MoteinoMega!!");
    #else
        Serial.println("else #1");  // both motinos end up here
    #endif
  #elif defined(__ARDUINO_ARC__)
    Serial.println("__ARDUINO_ARC__ was defined");
  #else
     Serial.println("else #2");
  #endif
#else
  Serial.println("SPI_HAS_EXTENDED_CS_PIN_HANDLING has been defined")
#endif


delay(3000);

}
