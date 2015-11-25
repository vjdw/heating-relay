#include <TimerOne.h> 
#include <stdbool.h>

//for wireless thermostats
#define MAX_LOW_COUNT 40
#define MIN_LOW_COUNT 24
#define MAX_HIGH_COUNT 14
#define MIN_HIGH_COUNT 6
#define MIN_NULL_COUNT 15
#define MAX_NULL_COUNT 28

//for wireless mains switches
#define MAX_LOW_COUNT_M 29
#define MIN_LOW_COUNT_M 22
#define MAX_HIGH_COUNT_M 10
#define MIN_HIGH_COUNT_M 6
#define MIN_NULL_COUNT_MS 6
#define MAX_NULL_COUNT_MS 10
#define MIN_NULL_COUNT_ML 23
#define MAX_NULL_COUNT_ML 28


#define COUNT_LIMIT 250
#define NOPACKETA 0
#define NOPACKETB 0
#define PACKETA 1
#define PACKETB 1

#define TIMER_US 50
//#define TIMER_US 50000

typedef enum { NONE, INIT, PREAMBLE, DATAHIGH, DATALOW, POSTAMBLE } PacketState;

enum stateA { preambleA, dataA};
//ver 1.0
//ver 1.1 changed MAX_NULL_COUNT from 23 t 28
//ver 1.2 increased transmit buffer size from 256 to 512 bytes
//ver 1.3 bounds check transmit buffer input pointer to maximum value of 509 
//ver 1.4 increase transmit buffer size from 512 to 1023 bytes
//ver 1.5 reduce transmit buffer size to 512 bytes

const int rxin = 12;    //radio receive module connects to this board pin
const int ledPin = 8;  //was led on pin 13, changed to pin 8 for tx out
char stateTrigger = 0;  //state machine trigger indicataor
char inStateA = 0;      //state machine type A decoder .. for room thermometer protocol
int rxLevel = 0;        //rx level read from radio module
int lastRXlevel = 0;   //previous rx level read from module
int countL = 0;        //count of successive rx low levels to determine pulse length
int countH = 0;        //count of successive rx high levels
int countLclone = 0;   //clone of rx low level pulse length for state machine
int countHclone = 0;  //ditto
int validPacketA = 0;  //a valid room temperature packet has been decoded
int validPacketB = 0;  //a valid wireless mains control signal has been detected
int theAPacket[64];      //array for compiling received data packet
int theBPacket[24];    //array for compiling wireless mains packets


//for testing only
char str[20];


//boiler preambles and postambles
#define BpreambleL 509
#define BpreambleH 89
#define BpostambleL 0
#define BpostambleH 0

//radiator preambles and postambles
#define RpreambleL 266
#define RpreambleH 0
#define RpostambleL 0
#define RpostambleH 8

//boiler data pattern
#define Bdata0A 19
#define Bdata0B 6
#define Bdata1A 7
#define Bdata1B 5

//radiator data pattern
#define Rdata0A 25
#define Rdata0B 8
#define Rdata1A 8
#define Rdata1B 25


typedef enum { RADIATOR, BOILER } packettype;
packettype packet = BOILER;
int inpointer, outpointer, repeat;
int txbuf[512];
int preamblea;
int preambleb;
int postamblea;
int postambleb;
int data0a;
int data0b;
int data1a;
int data1b;
bool locked;
int transmitTicks;
int lastTxBit;

#define PAYLOAD_LENGTH 24

//oneon =  0xeaeaaa
//oneoff = 0xeaeaab
//twoon =  0xeabaaa
//twooff = 0xeabaab
//threeon = 0xeaaeaa
//threeoff = 0xeaaeab
//fouron = 0xeaabaa
//fouroff = 0xeaabab
//bool oneOn[PAYLOAD_LENGTH] = {1,1,1,0, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};
bool channel2socket1On[PAYLOAD_LENGTH] = {1,0,1,1, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,0};
bool channel2socket1Off[PAYLOAD_LENGTH] = {1,0,1,1, 1,0,1,0, 1,1,1,0, 1,0,1,0, 1,0,1,0, 1,0,1,1};

int debug = 1;
/*
      Set up      serial comms on usb port
                  arduino pin 8 as transmit data
                  arduino pin 13 as received rf data input
                  timer1 as a 50 microsecond ISR timer
                  pin 13 has a led on it, set up as output for any desired indication 
*/
void setup() 
{
  // Serial comms, this is the usb serial port for communications to host raspberry pi or laptop
  Serial.begin(9600);    

  
  // Initialize the digital pin as an output, Pin 13 has an LED connected on most Arduino boards
  pinMode(ledPin, OUTPUT);  
  
  // setup for the reception of rf data
  // Initialize pin chosen for rf data input as an input
  //pinMode(rxin, INPUT);    

  // setup for the transmission of rf data
  locked = false;
  inpointer=outpointer=0;
  lastTxBit = LOW;
  digitalWrite(ledPin, LOW);
  repeat = 0;
  inpointer = outpointer = 0;


  
  // Initialize 50 us timer and attach isr to it
  Timer1.initialize(TIMER_US); 
  //Timer1.initialize(20000);    //good for low speed testing
  Timer1.attachInterrupt( timerIsr ); 
}

int loc = -1;
int toWrite = LOW;
int transmitTimeRemainingUS = 0;
volatile PacketState packetState = INIT;
//bool* packetPayload = channel2socket1Off;
volatile bool* packetPayload = channel2socket1On;

//for a wireless mains socket, the preamble is:  low of 13290 us
//                                  data  1 is:  high of 416 us then low of 1250 us
//                                  data  0 is:  high of 1270 us low 395 us
//                              a postamble of:  high of 416 us
void timerIsr()
{  
  if (transmitTimeRemainingUS <= 0)
  {
    if (packetState == NONE)
      return;
    else if (packetState == INIT || packetState == POSTAMBLE)
    {
      packetState = PREAMBLE;
    }
    else if (packetState == PREAMBLE)
    {
      loc = 0;
      packetState = DATAHIGH;
    }
    else if (packetState == DATAHIGH)
    {
      packetState = DATALOW;
    }
    else if (packetState == DATALOW)
    {
      loc++;
      
      if (loc == PAYLOAD_LENGTH)
      {
        loc = 0;
        packetState = POSTAMBLE;
      }
      else
      {
        packetState = DATAHIGH;
      }
    }
      
    if (packetState == PREAMBLE)
    {
      toWrite = LOW;
      transmitTimeRemainingUS = 13290;
    }

    else if (packetState == DATAHIGH)
    {
      toWrite = HIGH;
      transmitTimeRemainingUS = packetPayload[loc] ? 400 : 1250;
    }

    else if (packetState == DATALOW)
    {
      toWrite = LOW;
      transmitTimeRemainingUS = packetPayload[loc] ? 1250 : 400;
    }
      
    else if (packetState == POSTAMBLE)
    {
      toWrite = HIGH;
      transmitTimeRemainingUS = 416;
    }
  }

  //Serial.println("loc = " + String(loc) + " transmit " + String(toWrite));
  digitalWrite(ledPin, toWrite);
  transmitTimeRemainingUS -= TIMER_US;

}



/*
    Main code loop, this is the non-ISR code.
    
    Monitors global variable stateTrigger that is set by ISR to indicate an edge has been detected in
    received rf data stream, and if so invokes a state machine update by the rf data decoder routine.
   
     Monitors the global variable validPacketA that is returned by the rf data decoder routine to indicate
     a packet conforming to a room temperature sensor has been received.
     
     Invokes a crc check on a newly recieved validPacketA
     
     Informs host machine of valid packet content.
    
*/
void loop()
{
  packetPayload = channel2socket1On;
  packetState = INIT;
  delay(250);
  
  packetState = NONE;
  delay(500);
  
  packetPayload = channel2socket1Off;
  packetState = INIT;
  delay(250);
  
  packetState = NONE;
  delay(500);
  
//char i;

/* 

 Transmit data is sent via the 50 us ISR, and during this time receive data is ignored via the locked flag.
 For data to transmit as rf packets:
Serial input data format from the host python program. The format supports the construction of two different packet types,
one is for the boiler on/off and the other is for the wireless sockets, ready for wireless transmission. 
Allowing for construction of packets gives the flexibility of changing packet contents from the
python central heating controller program rather than changing this arduino code.

BOILER WIRELESS CONTROL
b  clear the transmit packet buffer and then append any following data to create a boiler code type packet


MAINS REMOTE SWITCHES
m  clear the mains socket packet and then append any following packet data to the "m" packet prior to transmission


r  increase the repeat transmission count by 1 for the entire preamble+packet+postamble
    data held in the transmit buffer

t  transmit the packet

0  append a zero to the packet being constructed
1  append a one to the packet being constructed
p  append a preamble code to the packet being constructed
P  append a postamble code to the packet being constructed


         for a boiler packet the preamble is:  low 25460 us then a high of 4430 us
                                   data 1 is:  low 333 us then a high of 270 us
                                   data 0 is:  low of 940 us then a high of 290 us
                     
                     
for a wireless mains socket, the preamble is:  low of 13290 us
                                  data  1 is:  high of 416 us then low of 1250 us
                                  data  0 is:  high of 1270 us low 395 us
                              a postamble of:  high of 416 us


The transmit buffer holds a list of integers representing the number of 50 microsecond ticks corresponding to the
on/off keyed rf data.  The transmit buffer will always start with an rf preamble off period (low), and then integer
values represent alternating on then off periods of rf. At completion of the packet transmission the transmitter
is turned off.


 
The locked flag delays processing serial input destined for rf transmission until completion of any data currently
being transmitted. If not locked this means data is not being transmitted so received rf data can be monitored, and 
commands from the host python program can be processed.
  
*/  
  /*
  if (!locked)  {
 

         //for safety just ensure transmitter is off
         digitalWrite(ledPin, LOW);        //always start with rf off
    
         // Read serial input:
        while (Serial.available() > 0) {
            
            int inChar = Serial.read();
            
            switch (inChar) {
            
            //radiator command is coming 
            case 'm':   packet = RADIATOR;
                        preamblea = RpreambleL;
                        preambleb = RpreambleH;
                        postamblea = RpostambleL;
                        postambleb = RpostambleH;
                        data0a = Rdata0A;
                        data0b = Rdata0B;
                        data1a = Rdata1A;
                        data1b = Rdata1B;
                        inpointer = 0;
                        outpointer = 0;
                        repeat = 0;
                        break;
            
            //boiler command is coming        
            case 'b':   packet = BOILER;
                        preamblea = BpreambleL;
                        preambleb = BpreambleH;
                        postamblea = BpostambleL;
                        postambleb = BpostambleH; 
                        data0a = Bdata0A;
                        data0b = Bdata0B;
                        data1a = Bdata1A;
                        data1b = Bdata1B;        
                        inpointer = 0;
                        outpointer = 0;
                        repeat = 0;
                        break;
  
  
            //packet repeat count
            case 'r':   repeat +=1;
                        break;
             
            //preamble request           
            case 'p':  if (preamblea != 0) {
                          txbuf[inpointer++] = preamblea;
                        }
                        if (preambleb !=0) {
                          txbuf[inpointer++] = preambleb;
                        }
                        break;
                        
                        
            //postamble request
            case 'P':   if (postamblea != 0) {
                            txbuf[inpointer++] = postamblea;
                        }
                        if (postambleb != 0 ) {
                            txbuf[inpointer++] = postambleb;
                        }
                        break;
                          
                          
           //append a data 0 bit pattern             
           case '0':    txbuf[inpointer++] = data0a;
                        txbuf[inpointer++] = data0b;
                        break;
                          
            //append a data 1 bit pattern
            case '1':   txbuf[inpointer++] = data1a;
                        txbuf[inpointer++] = data1b;
                        break;
             
           //lock then transmit the packet
             case 't': transmitTicks = 0;
                       outpointer = 0;
                       lastTxBit = HIGH;          // ensure message starts on a low bit
                       locked = true;
                       break;
                         
               
            //ignore unknown characters in command stream
             default:   
 /*                     packet = RADIATOR;
                        preamblea = RpreambleL;
                        preambleb = RpreambleH;
                        postamblea = RpostambleL;
                        postambleb = RpostambleH;
                        data0a = Rdata0A;
                        data0b = Rdata0B;
                        data1a = Rdata1A;
                        data1b = Rdata1B;
                        inpointer = 0;
                        outpointer = 0;
                        repeat = 0;
                        locked = false;
                        Serial.print("got a default " );
                        Serial.print(inChar);
                        
 */
 /*
                        break;
  
            }
            
          //bounds check buffer pointers
          if (inpointer > 500)
              inpointer = 500;
            
   
        }  //end of serial available
        
        
    }  //end of !locked 
    
 



  //RF data in handling after here
  //see if state machine update is required. This is requested by the received data tick ISR
  //detecting an edge in any incoming ook rf data stream.
  
  
   if ( stateTrigger == 1 )      //required
     {
     stateTrigger = 0;          //reset for next time
     
     validPacketA = stateUpdateA ( countHclone, countLclone );    //state machine for wireless thermometer packets
                                                                  //add further state machines
                                                                  //if other packets types need to be
                                                                  //decoded
     
     validPacketB = stateUpdateB ( countHclone, countLclone );    //state machine for wireless mains switches                                                           
                                                                  
     }
 
 
  
  if ((validPacketA == PACKETA) || (debug >0) )         //room temperature packet has been detected
    {

      validPacketA = NOPACKETA;

      Serial.print("Roomtempstart:");
      for ( i=0; i<48; i++) 
        {
       Serial.print(theAPacket[i]);
        }
      
     Serial.println("end");    
    debug=0;     

    }
    
    
   
  if ((validPacketB == PACKETB) || (debug >0) )         //wireless mains packet has been detected
    {

      validPacketB = NOPACKETB;

      Serial.print("Wirelessstart:");
      for ( i=0; i<24; i++) 
        {
       Serial.print(theBPacket[i]);
        }
      
     Serial.println("end");    
    debug=0;     

    }   
    
   */ 
    
}
 


/// --------------------------
/// Custom ISR Timer Routine
/// --------------------------
/*
    Measures high and low periods of received rx data in units of interrupt timer ticks.
    Counts are limited to COUNT_LIMIT to prevent any overflow problems.
    When a level change is seen in the received data the high or low period is cloned to
    a global variable and a global flag is set to indicate to the received data state machine
    that a state update is required as a function of the received data bit just received.
    
    inputs    reads data level on arduino pin 13
    
    outputs   countHclone     the number of timer interrupt ticks corresponding to the high
                              period of the last received radio data bit, this may be zero.
              countLclone     the nubmer of timer interrupt ticks corresponding to the low
                              period of the last received radio data bit, this may be zero.
              stateTrigger    set to 1 if an edge has been detected in received rf data stream
                              to indicate to the received data decode that a state update is
                              needed.
*/

void timerIsrOld()
{
    
    if (!locked) {
        //measure high and low periods of received rx data and pass to state maching
        rxLevel = digitalRead(rxin);
    
        if (rxLevel == lastRXlevel)  //same level as last time
          {    
          if ((rxLevel == 0) && (countL < COUNT_LIMIT))
              {
              countL += 1;
              countH = 0;
              } 
          else if ((rxLevel == 1) && (countH < COUNT_LIMIT))
            {
            countH += 1; 
            countL = 0;
            }
          }
          
          
        else                       //level change
          {
           stateTrigger = 1;      //state machine update needed
           lastRXlevel = rxLevel;
           countHclone = countH;
           countLclone = countL;
           countH = 0;
           countL = 0;
          }
         
         //as in main loop, for safety just ensure transmitter is off
         digitalWrite(ledPin, LOW);        //always start with rf off     
         
          
    } //end of !locked
   
   
   else {
   //locked, so must be transmitting
   
         transmitTicks-=1;

          //see if this bit has gone
         if (transmitTicks <= 0) {
 
              //need to load next bit, but see if there is one or whether the
              //end of message has been reached
              if ( inpointer == outpointer ) {
                
                  //message all gone, but there may be a pending message repeat
                  
                      if ( repeat <= 0 )  {
                        
                      //all gone so finish off this transmission
                      
                            digitalWrite(ledPin, LOW);
                            lastTxBit = LOW;
                            repeat = 0;
                            inpointer = outpointer = 0;
                            locked = false;
                      }  
                      else {
                      //still repeats to go so reload message
                      
                            digitalWrite(ledPin, LOW);        //always start with rf off
                            lastTxBit = LOW;                  //remember for next time
                            outpointer = 0;                   //reset buffer extraction pointer
                            transmitTicks = txbuf[outpointer++];
                            repeat -= 1;                      //another buffer repeat done
                            
                      }
                      
                }
               else   {
               // there are still message bits to go
               
               
                       transmitTicks = txbuf[outpointer++];  //get duration of next bit
                       
                       if (lastTxBit == LOW) {
                             digitalWrite(ledPin, HIGH);
                             lastTxBit = HIGH;
                       }
                       else {
                             digitalWrite(ledPin, LOW);
                             lastTxBit = LOW;
                       } 
                      

               } //end of message length count loop
         } // end of transmit ticks count loop     
   } //end of locked
}


/* 
  This routine is called when the 50 us timer ISR detects a level change in the received
  rf data stream and subsequently sets the stateTrigger flag to request a state update.
  As long as the data stream conforms to a valid packet from a room temperature sensor the data
  is appended to the global array theAPacket[]
 
 
      Lclone      number of 50 us ticks corresponding to the low data period of the previous
                  rf data bit, this may be zero
      Hclone      number of 50 us ticks corresponding to the high data perios of the previous
                  rf data bit, this may be zero 
                   
      return      NOPACKETA if a packet has not just been completed by the last rx data bit
                  PACKETA if a packet corresponding to a room temperature sensorhas just been completed


*/
int stateUpdateA ( int Hclone, int Lclone ) 
   {
    static char bitCount=0;            //bit count for each data field
                                       //preamble of eight 1 bits
                                       //24 data field bits
                                       //further preamble of eight 1 bits
                                       //8 crc field bits 
                                       
    static enum stateA instate = preambleA;
    int validnull;
    int validlow;
    int validhigh;
    char retvalue;
    int savenullvalue;
    
    retvalue = NOPACKETA;
    
    //check to see if this is a valid bit length
    //note, after the last 0 or 1 bit of a packet there will not be an extended null data period
    //as the rf carrier will be off from then on, leading to no validnull being received for end of packet.
    validlow = validhigh = validnull = false;
    if ((Lclone >= MIN_NULL_COUNT) && (Lclone <= MAX_NULL_COUNT))
        {
         validnull = true;
         savenullvalue=Lclone;
        }
    else if ((Hclone >= MIN_LOW_COUNT) && (Hclone <= MAX_LOW_COUNT))
        {
         validlow = true;
        }
    else if ((Hclone >= MIN_HIGH_COUNT) && (Hclone <= MAX_HIGH_COUNT))
        {
         validhigh = true; 
        }
  

  
    //if not a valid bit length reset the state machine
    if (( validnull == false) && (validlow == false) && (validhigh == false))
        {
         instate = preambleA;
         bitCount = 0; 
        }
    
    
    //if it was a valid bit length keep compiling the current packet
   
    else              //must be valid high, low or null to get into here
        {
 
          
        //check for end of packet
       
        switch (instate)
            {
            case preambleA:
            
                if (validhigh==true)
                    {
                    theAPacket[bitCount]=1;
                    bitCount += 1;
                    if (bitCount > 8) 
                      bitCount=8;

                    }
                else if ((validlow==true) && (bitCount > 5) )    //assume missed start of preamble and into data field
                    {
                    theAPacket[8]=0;                      //assume this is the first data bit following the preamble of ones
                    instate = dataA;                    //into data section of packet
                    bitCount=8; 
                   
                    }
                else if ((validlow==true) && (bitCount <= 5) )    //not enough preamble bits so restart looking for them
                    {
                    bitCount=0;                  //start looking for a long preamble again
                    
                    }

                  //no need to do valid null state for preamble
                                    
                  break;
   
            case dataA:
               if (validhigh==true)
                    {
                     bitCount+=1;
                     theAPacket[bitCount]=1;
                     
                     if (bitCount >= 47)
                       {
                         
                         bitCount=0;        //packet completed, reset state machine for next packet
                         instate = preambleA;
                         retvalue=PACKETA;
                                                
                       }
                     
                    }
               else if (validlow==true)
                   {
                     bitCount+=1;
                     theAPacket[bitCount]=0;
                     
                     if (bitCount >= 47)
                         {
                         
                         bitCount=0;        //packet completed, reset state machine for next packet
                         instate = preambleA;
                         retvalue=PACKETA;
                                                
                         }
                   }
 
                   break; 
               
             default:
                 bitCount=0;
                 instate = preambleA;
                 break;
            }
       }
         
         
   return retvalue; 

  }
 

/* 

For received rf data:
 This routine is called when the 50 us timer ISR detects a level change in the received
 rf data stream. As long as the data stream conforms to a valid packet from a wireless mains
 switch the data is appended to the global array theBPacket[]
 
 
      Lclone      number of 50 us ticks corresponding to the low data period of the previous
                  rf data bit, this may be zero
      Hclone      number of 50 us ticks corresponding to the high data perios of the previous
                  rf data bit, this may be zero 
                   
      return      NOPACKETB if a packet has not just been completed by the last rx data bit
                  PACKETB if a packet corresponding to a room temperature sensorhas just been completed

*/
int stateUpdateB( int Hclone, int Lclone ) 
   {
    static char bitCount=0;            //bit count for each data field
                                       //24 data bits following rf off period
                                       

    int validnull;
    int validlow;
    int validhigh;
    char retvalue;
    
    retvalue = NOPACKETB;
    
    //check to see if this is a valid bit length
    //note, after the last 0 or 1 bit of a packet there will not be an extended null data period
    //as the rf carrier will be off from then on, leading to no validnull being received for end of packet.
    validlow = validhigh = validnull = false;
   
   if (((Lclone >= MIN_NULL_COUNT_MS) && (Lclone <= MAX_NULL_COUNT_MS)) ||
      ((Lclone >= MIN_NULL_COUNT_ML) && (Lclone <= MAX_NULL_COUNT_ML)))
        {
         validnull = true;
        }
    else if ((Hclone >= MIN_LOW_COUNT_M) && (Hclone <= MAX_LOW_COUNT_M))
        {
         validlow = true;
        }
    else if ((Hclone >= MIN_HIGH_COUNT_M) && (Hclone <= MAX_HIGH_COUNT_M))
        {
         validhigh = true; 
        }
  

  
    //if not a valid bit length reset the state machine
    if (( validnull == false) && (validlow == false) && (validhigh == false))
        {
         bitCount = 0; 
        }
    
    
    //if it was a valid bit length keep compiling the current packet
   
    else              //must be valid high, low or null to get into here
        {
 
          
        //check for end of packet
        if (validhigh==true)
              {
               theBPacket[bitCount]=1;
               bitCount+=1;

               if (bitCount >= 24)
                   {
                   bitCount=0;        //packet completed, reset state machine for next packet
                   retvalue=PACKETB;
                   }
                     
              }
       else if (validlow==true)
              {
              theBPacket[bitCount]=0;
              bitCount+=1;
              if (bitCount >= 24)
                    {
                    bitCount=0;        //packet completed, reset state machine for next packet
                    retvalue=PACKETB;
                    }
              }
       }
         
         
   return retvalue; 

  }
 







