// Use ATTiny 85 as watchdog timer

const uint32_t TIMEOUT_MIN = 10;  // Watchdog timeout in minutes
const byte HEARTBEAT_INPUT =  3;
const byte LED_RED =          0;  // connected tor Red/Green LED
const byte LED_GRN =          1; 
const byte RELAY =            2; // controls power to device being monitored
const uint32_t MINUTE =   60000;  // one minute for millis() timer

uint32_t g_heartbeat_time_high = 0;
uint32_t g_heartbeat_time_low =  0;


// Function prototypes
void rebootRemoveDevice();



void setup() 
{
  pinMode(HEARTBEAT_INPUT, INPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GRN, OUTPUT);
  pinMode(RELAY, OUTPUT);
  
  rebootRemoveDevice();
  
} // end setup()

void loop() 
{
  // Check high heartbeat
  if ( digitalRead(HEARTBEAT_INPUT) == HIGH )
  { g_heartbeat_time_high = millis(); }

  // Check low heartbeat
  if ( digitalRead(HEARTBEAT_INPUT) == LOW )
  { g_heartbeat_time_low = millis(); }

  // Need to get a high and low heartbeat
  bool  isOkHighHeartbeat = (long)(millis() - g_heartbeat_time_high) < (TIMEOUT_MIN * MINUTE);
  bool  isOkLowHeartbeat =  (long)(millis() - g_heartbeat_time_low)  < (TIMEOUT_MIN * MINUTE);
  if (!isOkHighHeartbeat || !isOkLowHeartbeat )
  {
    // turn on red LED
    digitalWrite(LED_RED, HIGH);
    digitalWrite(LED_GRN, LOW);

    digitalWrite(RELAY, LOW); // Turn relay off to power down monitored device
    delay(10000); // wait 10 seconds
    
    rebootRemoveDevice();  // Restore power
  }
  
  // Green LED should mimic the heartbeat input
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GRN, digitalRead(HEARTBEAT_INPUT));
  
}  // end loop()

void rebootRemoveDevice()
{
  digitalWrite(RELAY, HIGH); // turn relay on
  
  // Turn LED yellow to indicate waiting in setup
  analogWrite(LED_RED, 127);
  analogWrite(LED_GRN, 127);
  delay(MINUTE * 5); // just wait here for five minutes
  
  // Turn LED off
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GRN, LOW);
  
  // reset timers
  g_heartbeat_time_high = millis();
  g_heartbeat_time_low = millis();
  
}  // end rebootRemoveDevice()

