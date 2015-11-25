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

#define TIMER_US 50
#define SIGNAL_LENGTH 24

// Packets consist of preamble > signal data > postamble.
typedef enum { NONE, INIT, PREAMBLE, DATAHIGH, DATALOW, POSTAMBLE } PacketState;

// These are the signals that control the remote sockets.
bool channel2_socket1_OnSignal[SIGNAL_LENGTH] =  {1,0,1,1, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};
bool channel2_socket1_OffSignal[SIGNAL_LENGTH] = {1,0,1,1, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,1};
bool channel2_socket2_OnSignal[SIGNAL_LENGTH] =  {1,0,1,1, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,0, 1,0,1,0};
bool channel2_socket2_OffSignal[SIGNAL_LENGTH] = {1,0,1,1, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,0, 1,0,1,1};
bool channel2_socket3_OnSignal[SIGNAL_LENGTH] =  {1,0,1,1, 1,0,1,0, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,0};
bool channel2_socket3_OffSignal[SIGNAL_LENGTH] = {1,0,1,1, 1,0,1,0, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,1};
// doesn't work? bool channel2_socket4_OnSignal[SIGNAL_LENGTH] =  {1,1,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,0};
// doesn't work? bool channel2_socket4_OffSignal[SIGNAL_LENGTH] = {1,1,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,1, 1,0,1,0, 1,0,1,1};

const int txPin = 8;

volatile bool* activeSignal = channel2_socket2_OnSignal;
int activeSignalIndex = -1; // The index of the bit currently being transmitted.

int currentTxLevel = LOW;
int timeRemainingAtCurrentTxLevelUS = 0;
volatile PacketState packetState = INIT;


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
  
  // Initialize 50 us timer and attach isr to it
  Timer1.initialize(TIMER_US); 
  Timer1.attachInterrupt(timerIsr); 
}

////////////////////////////////////////////////////////////////////////////////
// Main code loop, this is the non-ISR code.
////////////////////////////////////////////////////////////////////////////////
void loop()
{
  // Alternate on/off for testing.
  
  activeSignal = channel2_socket2_OnSignal;
  packetState = INIT;
  delay(250);
  
  packetState = NONE;
  delay(500);
  
  activeSignal = channel2_socket2_OffSignal;
  packetState = INIT;
  delay(250);
  
  packetState = NONE;
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
