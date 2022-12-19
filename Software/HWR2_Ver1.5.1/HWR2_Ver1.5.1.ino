/*
   CW flea transmitter, Ver 1.5.1 Copyright 2021 Kevin Loughin, KB9RLW
   Extended by ZachTek for their multiband version

   This program is free software, you can redistribute in and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation.
   http://www.gnu.org/licenses

   This software is provided free of charge but without any warranty.
   1.3 Fist test version for Revision 2 of the hardware.
   1.4 First version for the Revision 3 of the hardware
   1.5 Changed Up and Down buttons as they had been switched in the new hardware revision
   1.5.1 Fixed tuning speed bug.

*/

// Include the etherkit si5351 library.  You may have to add it using the library
// manager under the sketch/Include Library menu.

#include "si5351.h"
#include "Wire.h"
Si5351 si5351;


//Harware defines for the current design
#define LED40m 5       //Output to Green LED that indicates 40m band 
#define LED30m 6       //Output toGreen LED that indicates 30m band 
#define LED20m 7       //Output toGreen LED that indicates 20m band 
#define FilterRelay A2 //Output to Relay that will pick etiher 40m filter or 30/20m filter.  LOW= 
#define TXRelay A3     //Output to RX/TX relay. LOW=
#define BandButton 2   //pin number on the BandButton
#define tuneup A1       //pin number used for tuning up in freq
#define tunedn A0       //pin number used for tuning down in freq
#define MorseKey1 8    //Ring on the 3.5mm plug that goes to the Morse key
#define MorseKey2 9    //Tip on the 3.5mm plug that goes to the Morse key


//Helper constants that make the code more readable
#define Band40m 0
#define Band30m 1
#define Band20m 2
#define Pressed 0      //If a button is pressed the input is zero
#define KeyDown True;


// Declare variables and flags we will use in the program
int tunecount = 0; //delay counter between slow tuning and fast tuning
int tunestep = 1000; //minimum tuning step in hundreths Hertz
int tuning = 0; //flag to indicate tuning state
int tunevfo = 0; //flag to indicate tuning vfo state
int BandButtonState = 1; //flag for the band button
int buttonup = 1; //flag for tune up button
int buttondn = 1; //flag for tune down button
int tail = 0;  // The counter for the keying tail delay
int taildefault = 500; // Default to 1 second tail, change if you want.
boolean keyed; //flag to indicate keyed state
int keyedprev = 1; //flag to indicate previous key state after a change
int unkeyflag = 0; //flag for end of timeout switch back to RX
long freq = 703000000; //current frequency in centi Hertz.

long Band40mbottom = 700000000; //Lowest frequency of 40m Band CentiHertz. 100CentiHertz=1Hz
long Band40mtop = 707000000; //Highest frequency 40m Band
long Band30mbottom = 1010000000; //Lowest frequency in 30m Band
long Band30mtop = 1013000000; //Highest frequency in 30m Band
long Band20mbottom = 1400000000; //Lowest frequency in 20m Band
long Band20mtop = 1407000000; //Highest frequency in 20m Band


uint32_t ReferenceFrequency = 26000000; //TCXO Frequency used as a reference for the Si5351PLL
uint8_t CurrentBand = Band40m; //Keeps track on what band we are using. Start in 40m.
long  Currentbandtop, Currentbandbottom; //Keeps tracks of the bandedges of the current selected band


void setup() {
  // Serial.begin(9600); // enable serial for debugging output
  // Setup the I/O pins and turn the relay off
  pinMode (LED40m, INPUT);//These are outputs to turn on LEDs but due to sharing a current limit resistor they have to be set as input when not turned on
  pinMode (LED30m, INPUT);
  pinMode (LED20m, INPUT);
  pinMode (FilterRelay, OUTPUT); //Low=40m BP Filter. High=30m/20m BP Filter
  digitalWrite(FilterRelay, LOW); //Pick 40m BP filter
  pinMode (TXRelay, OUTPUT); //Low= Receive, High= Transmit
  digitalWrite(TXRelay, LOW); //Set to Receive

  pinMode (BandButton, INPUT_PULLUP); //Set BandButton pin as input with pullup enabled
  pinMode (MorseKey1, INPUT_PULLUP); // Morse Key input line
  pinMode (MorseKey2, INPUT_PULLUP); // Morse Key second input line
  //digitalWrite(keypin, HIGH); // turn on internal pullup resistor
  pinMode (tuneup, INPUT_PULLUP); // tune switch input line
  //digitalWrite(tuneup, HIGH); // turn off internal pullup resistor
  pinMode (tunedn, INPUT_PULLUP); // tune switch input line
  //digitalWrite(tunedn, HIGH); // turn off internal pullup resistor
  Serial.print("CW Flea. Version 1.4");
  StartupLEDBlink();
  SetBand(CurrentBand); //Pick the correct Low Pass filter



  si5351.init(SI5351_CRYSTAL_LOAD_8PF, ReferenceFrequency, 0); //initialize the VFO
  si5351.set_freq(freq, SI5351_CLK0);  //set initial freq
  si5351.set_freq(freq, SI5351_CLK1);
  si5351.output_enable(SI5351_CLK0, 0); //turn off outputs
  si5351.output_enable(SI5351_CLK1, 0);
  Currentbandtop, Currentbandbottom;

  // Setup the interrupt for the timer interrupt routine
  OCR0A = 0xAF;
  TIMSK0 |= _BV(OCIE0A);
  //
}

// interrupt handler routine.  Handles the timing counters, runs 1000 times per second
SIGNAL(TIMER0_COMPA_vect)
{
  if ( (tail > 0) && keyed ) // If we're in the tail period, count down
  {
    tail--;
  }
  if (tunecount) // button press counter, increment if button currently pressed
  {
    tunecount++;
  }
}

// This is the main program loop. Runs continuously:
void loop()
{

  // Read state of all inputs. note: 0=key or button pressed, 1=not pressed
  keyed = digitalRead (MorseKey2); 
  //&& digitalRead (MorseKey1) ); //Both the ring and the tip on the 3.5mm jack to the morse key is checked, if either one is shorted to ground 
  buttonup = digitalRead (tuneup); //read tuneup button state
  buttondn = digitalRead (tunedn); //read tunedn button state
  BandButtonState = digitalRead(BandButton); //read Band button state

  if (tunecount && buttonup && buttondn) // neither tune button pressed, assure pressed timer is zero
  {
     tunecount = 0;
  }
  if (!tunecount && (!buttonup || !buttondn))  // either tune button pressed reset timer once
  {
    tunecount = 1;
  }
  
  // Keying section, check key state, turn output on and off

  // Checking the timer if we're in the tail period
  if ( !tail && unkeyflag ) // timeout counted down, switch back to RX mode
  {
    digitalWrite (TXRelay, LOW); // turn off TR relay
    unkeyflag = 0; //reset the flag
  }

  // Keying logic next

  if ( keyed && !keyedprev) // did we just unkey? set the timeout value, turn off VFO
  {
    tail = taildefault;
    keyedprev = 1;
    si5351.output_enable(SI5351_CLK0, 0); // turn off VFO
    unkeyflag = 1; // flag for tail routine that we just unkeyed
  }

  if (!keyed && keyedprev) // did we just key down?
  {
    digitalWrite(TXRelay, HIGH); // switch TR relay
    delay (10); // wait 10 millisecond
    si5351.output_enable(SI5351_CLK0, 1); //turn on VFO
    keyedprev = 0; //set the flag so we know we keyed last time through.
  }

  // Tuning logic next

  if (buttonup && buttondn && tuning) //if no buttons are pressed, reset tuning flag
  {
    tuning = 0;
  }
  if (!buttonup || !buttondn) //either tune button pressed?
  {
    tuning = 1; //set flag to indicate button pressed
    if (!tunevfo) //turn on VFO if its off and set vfo flag
    {
      si5351.output_enable(SI5351_CLK1, 1);
      tunevfo = 1;
    }
    if (!buttonup) //tuning up in freq
    {
      if (tunecount < 500) // Move freq in low speed step
      {
        freq = freq + tunestep ;
      }
      if (tunecount > 499) // Move freq in high speed step
      {
        freq = freq + (tunestep * 10) ;
      }
      if (freq > Currentbandtop) // If we reached band edge, roll over
      {
        freq = Currentbandbottom ;
      }
      si5351.set_freq(freq, SI5351_CLK0);  //set vfo freq
      si5351.set_freq(freq, SI5351_CLK1);
    }
    if (!buttondn) //tuning down in freq
    {
      if (tunecount < 500) // Move freq in low speed step
      {
        freq = freq - tunestep ;
      }
      if (tunecount > 499) // Move freq in high speed step
      {
        freq = freq - (tunestep * 10) ;
      }
      if (freq < Currentbandbottom) // If we reached band edge, roll over
      {
        freq = Currentbandtop ;
      }
      si5351.set_freq(freq, SI5351_CLK0);  //set vfo freq
      si5351.set_freq(freq, SI5351_CLK1);
    }
  }


  if (tunevfo && !tuning) //buttons released, delay done, turn off VFO
  {
    tunevfo = 0;    
    delay (100);
    si5351.output_enable(SI5351_CLK1, 0); //turn off VFO

  }

  if (BandButtonState == Pressed)
  {
    NextBand();
  }
}

//Flashes the band LEDs to indicate Booting
void StartupLEDBlink()
{
  for (int i = 0; i < 10; i++)
  {
    pinMode (LED40m, OUTPUT);
    digitalWrite(LED40m, HIGH);
    delay (30);
    pinMode (LED40m, INPUT);

    pinMode (LED30m, OUTPUT);
    digitalWrite(LED30m, HIGH);
    delay (30);
    pinMode (LED30m, INPUT);

    pinMode (LED20m, OUTPUT);
    digitalWrite(LED20m, HIGH);
    delay (30);
    pinMode (LED20m, INPUT);
    delay (30);
  }
}

//Turn off all Band LEDs
void BandLEDsOff()
{
  pinMode (LED40m, INPUT);
  pinMode (LED30m, INPUT);
  pinMode (LED20m, INPUT);
}

//Pick the next band
void NextBand()
{
  CurrentBand++;//Advance state machine for what band is in use, if reached the last band -roll over to the lowest
  if (CurrentBand > Band20m) CurrentBand = Band40m;
  SetBand (CurrentBand);
  delay(100); //Simple debounce
  do {
    delay(10);
  } while (digitalRead(BandButton) == Pressed); //Wait for the BandButton to be released
}


void SetBand(uint8_t BandNum)
{
  BandLEDsOff ();//Turn off all Band LEDs
  switch (BandNum) {
    case Band20m :
      digitalWrite(FilterRelay, HIGH); //Pick 20/30m BP filter
      pinMode (LED20m, OUTPUT);
      digitalWrite(LED20m, HIGH);
      Currentbandtop = Band20mtop;
      Currentbandbottom = Band20mbottom;
      break;

    case Band30m:
      digitalWrite(FilterRelay, HIGH); //Pick 20/30m BP filter
      pinMode (LED30m, OUTPUT);
      digitalWrite(LED30m, HIGH);
      Currentbandtop = Band30mtop;
      Currentbandbottom = Band30mbottom;
      break;

    case Band40m:
      digitalWrite(FilterRelay, LOW); //Pick 40m BP filter
      pinMode (LED40m, OUTPUT);
      digitalWrite(LED40m, HIGH);
      Currentbandtop = Band40mtop;
      Currentbandbottom = Band40mbottom;
      break;
  }
  freq = Currentbandbottom;
}
