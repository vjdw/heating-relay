#include <TimerOne.h> 
#include <stdbool.h>

////////////////////////////////////////////////////////////////////////////////
// For a wireless mains socket, the preamble is:  low of 13290 us
//                                   data  1 is:  high of 416 us then low of 1250 us
//                                   data  0 is:  high of 1270 us low 395 us
//                               a postamble of:  high of 416 us
////////////////////////////////////////////////////////////////////////////////
#define PREAMBLE_LOW_DURATION_US 13290
#define POSTAMBLE_HIGH_DURATION_US 416
#define DATA_LEVEL_SHORT_US 400
#define DATA_LEVEL_LONG_US 1250
#define REPEAT_OFF_SIGNAL_MS 10800000 // 3 hours

#define MAIN_LOOP_US 65
#define TIMER_US 50
#define SIGNAL_LENGTH 24

// Packets consist of preamble > signal data > postamble.
typedef enum { NONE, INIT, PREAMBLE, DATAHIGH, DATALOW, POSTAMBLE } PacketState;

// These are the signals that control the remote sockets..
bool channel2_socket1_OnSignal[SIGNAL_LENGTH] =  {1,0,1,1, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};
bool channel2_socket1_OffSignal[SIGNAL_LENGTH] = {1,0,1,1, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,1};
bool channel2_socket2_OnSignal[SIGNAL_LENGTH] =  {1,0,1,1, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,0, 1,0,1,0};
bool channel2_socket2_OffSignal[SIGNAL_LENGTH] = {1,0,1,1, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,0, 1,0,1,1};
bool channel2_socket3_OnSignal[SIGNAL_LENGTH] =  {1,0,1,1, 1,0,1,0, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,0};
bool channel2_socket3_OffSignal[SIGNAL_LENGTH] = {1,0,1,1, 1,0,1,0, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,1};
bool channel2_socket4_OnSignal[SIGNAL_LENGTH] =  {1,0,1,1, 1,0,1,0, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,0};
bool channel2_socket4_OffSignal[SIGNAL_LENGTH] = {1,0,1,1, 1,0,1,0, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,1};

////// BEGIN Globals for transmission state //////
const int txPin = 8;

volatile bool* activeSignal = channel2_socket1_OffSignal;
int activeSignalIndex = -1; // The index of the bit currently being transmitted.

int currentTxLevel = LOW;
int timeRemainingAtCurrentTxLevelUS = 0;
volatile PacketState packetState = INIT;
volatile bool StopBroadcast = true;
////// END Globals for transmission state   //////

////// BEGIN Globals for monitoring central heating state //////
const int centralHeatingInPin = 12;
const int analogCentralHeatingInPin = 5;
int centralHeatingStateChangeCount = 0;
int offOnOffCount = 0;
bool confirmedCentralHeatingState = false;
////// END Globals for monitoring central heating state   //////

long repeatOffSignalCountdown = REPEAT_OFF_SIGNAL_MS;
int offOnOffTimer = 0;

////////////////////////////////////////////////////////////////////////////////
// Set up      timer1 as a 50 microsecond ISR timer
//             arduino pin 8 as transmit data
////////////////////////////////////////////////////////////////////////////////
void setup() 
{
  // Serial comms, this is the usb serial port for communications to host raspberry pi or laptop
  Serial.begin(9600);    
  
  // Initialize the digital pin as an output,
  pinMode(txPin, OUTPUT);  
  digitalWrite(txPin, LOW);

  pinMode(centralHeatingInPin, INPUT);
  
  // Initialize 50 us timer and attach isr to it
  Timer1.initialize(TIMER_US); 
  Timer1.attachInterrupt(timerIsr); 
}

////////////////////////////////////////////////////////////////////////////////
// Main code loop, this is the non-ISR code.
////////////////////////////////////////////////////////////////////////////////
void loop()
{
//  // Alternate on/off for testing.
//  activeSignal = channel2_socket1_OnSignal;
//  packetState = INIT;
//  delay(250);
//  packetState = NONE;
//  delay(500);
//  activeSignal = channel2_socket1_OffSignal;
//  packetState = INIT;
//  delay(250);
//  packetState = NONE;
//  delay(500);

  delay(MAIN_LOOP_US);

  offOnOffTimer -= MAIN_LOOP_US;
  if (offOnOffTimer < 0)
    offOnOffTimer = 0;

  repeatOffSignalCountdown -= MAIN_LOOP_US;
  if (repeatOffSignalCountdown < 0)
    repeatOffSignalCountdown = 0;

  int observedAnalogCentralHeatingState = analogRead(analogCentralHeatingInPin);
  bool observedCentralHeatingState = observedAnalogCentralHeatingState > 60;

  if (observedCentralHeatingState != confirmedCentralHeatingState)
  {
    centralHeatingStateChangeCount++;
  }
  else
  {
    centralHeatingStateChangeCount--;
    if (centralHeatingStateChangeCount < 0)
      centralHeatingStateChangeCount = 0;
  }

  //Serial.println(" analogIn:" + String(observedAnalogCentralHeatingState) + " digitalIn:" + String(confirmedCentralHeatingState) + " observed:" + String(observedCentralHeatingState) + " changeCount:" + String(centralHeatingStateChangeCount));

  if (centralHeatingStateChangeCount > 9)
  {
    centralHeatingStateChangeCount = 0;
    confirmedCentralHeatingState = observedCentralHeatingState;
    offOnOffCount++;

    // Start 7 second timer to check for Off-On-Off signal. 
    if (offOnOffTimer == 0)
      offOnOffTimer = 7000;
  }
  
  if (offOnOffTimer < 1000 && offOnOffCount > 0)
  {
    if (confirmedCentralHeatingState)
    {
      // Switch everything on.
      sendSignal(channel2_socket1_OnSignal);
      delay(1250);
      sendSignal(channel2_socket2_OnSignal);
      delay(1250);
    }
    else
    {
      // Off-On-Off means we should leave the sitting room heater on.
      bool leaveSittingRoomOn = offOnOffCount > 2;
      if (leaveSittingRoomOn)
      {
        // Reset the repeat-signal 3 hour timer, so sitting room will stay on for 3 hours.
        repeatOffSignalCountdown = REPEAT_OFF_SIGNAL_MS;
      }
      else
      {
        sendSignal(channel2_socket1_OffSignal);
        delay(1250);
      }
      
      // Always turn off spare room heater.
      sendSignal(channel2_socket2_OffSignal);
      delay(1250);
    }
    
    offOnOffTimer = offOnOffCount = 0;
  }  
  
  if (repeatOffSignalCountdown == 0)
  {
    // For safety, only the off-state signal is repeated.  
    if (!confirmedCentralHeatingState)
    {
      sendSignal(channel2_socket1_OffSignal);
      delay(1250);
      sendSignal(channel2_socket2_OffSignal);
      delay(1250);
    }
    repeatOffSignalCountdown = REPEAT_OFF_SIGNAL_MS;
  }
}

void sendSignal(bool* socketSignal)
{
  StopBroadcast = false;
    
  activeSignal = socketSignal;
  
  // Broadcast the active signal.
  packetState = INIT;
  delay(500);
  
  // Stop broadcast.
  packetState = NONE;
  StopBroadcast = true;
  delay(500);
}

////////////////////////////////////////////////////////////////////////////////
// Called by interrupt every 50 us.
////////////////////////////////////////////////////////////////////////////////
void timerIsr()
{  
  if (timeRemainingAtCurrentTxLevelUS <= 0)
  {
    moveToNextPacketState();

    setTxLevelAndTime();

    digitalWrite(txPin, currentTxLevel);
  }

  timeRemainingAtCurrentTxLevelUS -= TIMER_US;
}

void moveToNextPacketState()
{
  if (StopBroadcast)
    packetState = NONE;
    
  switch (packetState)
  {
    case NONE:
      break;
    case PREAMBLE:
      activeSignalIndex = 0;
      packetState = DATAHIGH;
      break;
    case DATAHIGH:
      packetState = DATALOW;
      break;
    case DATALOW:
      activeSignalIndex++;
      packetState = (activeSignalIndex < SIGNAL_LENGTH)
                  ? DATAHIGH    // More data bits to write in current packet.
                  : POSTAMBLE;  // End of data section of this packet.
      break;
    case POSTAMBLE:
    case INIT:
      packetState = PREAMBLE;
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Based on current packet state and active data signal, set transmission level and time.
// e.g. transmit low signal for 400 us.
////////////////////////////////////////////////////////////////////////////////
void setTxLevelAndTime()
{
  switch (packetState)
  {
    case NONE:
      currentTxLevel = LOW;
      timeRemainingAtCurrentTxLevelUS = TIMER_US;
      break;
    case PREAMBLE:
      currentTxLevel = LOW;
      timeRemainingAtCurrentTxLevelUS = PREAMBLE_LOW_DURATION_US;
      break;
    case DATAHIGH:
      // Binary 1 is transmitted as short-high then long-low.
      // Binary 0 is transmitted as long-high then short-low.
      currentTxLevel = HIGH;
      timeRemainingAtCurrentTxLevelUS = activeSignal[activeSignalIndex]
                                      ? DATA_LEVEL_SHORT_US
                                      : DATA_LEVEL_LONG_US;
      break;
    case DATALOW:
      currentTxLevel = LOW;
      timeRemainingAtCurrentTxLevelUS = activeSignal[activeSignalIndex]
                                      ? DATA_LEVEL_LONG_US
                                      : DATA_LEVEL_SHORT_US;
      break;
    case POSTAMBLE:
      currentTxLevel = HIGH;
      timeRemainingAtCurrentTxLevelUS = POSTAMBLE_HIGH_DURATION_US;
      break;
  }
}
