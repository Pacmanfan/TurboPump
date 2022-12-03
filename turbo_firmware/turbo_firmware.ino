
/*
This is to control the turbo drag pump.
This is a Holweck pump Alcatel model 5006 2H

Basically, it's a single cpil with a hall effect sensor
I beleive it gets up to 27-30K rpm
We need to ramp up the speed over time (the other manual said 3 mins)
We monitor the speed with the hall effect
Still not sure about starting the pumpo in a spcific direction
Maybe the firmware can support an LED to show when:
stopped  - red
spinning up to speed - yellow
at speed - green

We're going to use a nano for this


looks like the duty cycle should be around 50% until it gets to about 7K
then reduce to 35%


We're using an external mosfet to power
- look into diode protection

*/

#define PIN_MOSFET            3 // so we can use PWM to control current
#define PIN_HALLSENSOR        A0
#define PIN_LED_RED           A1
#define PIN_LED_GREEN         A2
#define PIN_LED_BLUE          A3
#define PIN_BUTTON            5 // the button will toggle between spin up or spin down
#define DEBOUNCE_DELAY        50    // the debounce time in milliseconds; 

double DUTY_CYCLE = 50; // go to 35 when at higher speeds
double TRIGGER_ANGLE    =    95; // 90/ // the angle / phase that we should send the pulse
int PWMLEVEL = 255;

/*
The position of the trigger angle will affect which way it spins
With a trigger angle of 2-5%, it will spin clockwise (not the direction we want)
First attemps at a trigger angle of 55% make it spin CC
*/
double curRPM = 0; // current RPM (measured from pulses)
double curfrequency = 0; // current frequency (measured from pulses)
//variables for debouncing the button
int SelectButtonState = HIGH;       // the current reading from the input pin
int lastSelectButtonState = HIGH;   // the previous reading from the input pin
unsigned long lastSelectDebounceTime = 0;  // the last time the Select pin was toggled

#define NUMRPMLOG 60 // keep 1 mintue data
int rpmlog[NUMRPMLOG];
int rpmidx = 0;


//variables for sending a 1Hz update
unsigned long nextOneSecondTimer = 0;
unsigned long lastperiod_duration_uS = 0; // the time in uSecond that the previous period took
unsigned long lasthighstart_uS = 0; // the time the last period (rising edge) started
unsigned long laststatechangetime_uS = 0; // the last time the state changed

// phase tracking

int triggered_this_cycle = 0 ; // have we sent a pulse this cycle yet?
int triggered_this_cycle2 = 0 ; // have we sent a pulse this cycle yet?
unsigned long pulse_end_time = 0; // after the pulse is triggered, when should we end it.

//we need some sort of state to know what we're doing
enum eSTATE
{
  eIdle, // This is the state when the device is not running
  eKickStart, // start the motor up in the right direction, may take a little doing...
  eRun, // spinning up to speed and maintaining speed
 
};

eSTATE curstate = eIdle;//eSpinUp;//eIdle; // the applistion starts off with the state as idle

void setup() 
{
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_MOSFET, OUTPUT);
  pinMode(PIN_HALLSENSOR,INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

 
  //TCCR2B = TCCR2B & B11111000 | B00000001; // for PWM frequency of 31372.55 Hz on timer 2

  Serial.begin(115200);// initialize serial communication    
  Serial.println("Turbo Pump firmware version 1");
  Serial.println("Steve Hernandez 2022");
  Serial.println("Commands:");
  PrintHelpStats();
}

void PrintHelpStats()
{
    Serial.print("(d) Duty Cycle = " );
    Serial.println( DUTY_CYCLE);
    Serial.print("(p) Trigger angle = " );
    Serial.println( TRIGGER_ANGLE);      
    Serial.println("(k) Kickstart " );
    Serial.println("(s) Start/Stop motor " );  
}
/*
void RGB_color(int red_light_value, int green_light_value, int blue_light_value)
 {
  analogWrite(PIN_LED_RED, red_light_value);
  analogWrite(PIN_LED_GREEN, green_light_value);
  analogWrite(PIN_LED_BLUE, blue_light_value);
}
*/

void DoSerialInput()
{
  static String sbuf = "";
  while (Serial.available())
  {
    char c = Serial.read();
    sbuf += c;
  }
  if(sbuf.indexOf("\n") > 0)
  {
    //process it
    Serial.println("processing input");
    Serial.println(sbuf);
    if(sbuf.indexOf("p") != -1)
    {
      //phase angle   
      String tmp = sbuf.substring(1);
      double tv = tmp.toFloat();
      TRIGGER_ANGLE = tv;
      Serial.print("New trigger angle = " );
      Serial.println( TRIGGER_ANGLE);
    }else if (sbuf.indexOf("d") != -1)
    {
      String tmp2 = sbuf.substring(1);
      double tv1 = tmp2.toFloat();
      DUTY_CYCLE = tv1;
      Serial.print("New Duty Cycle = " );
      Serial.println( DUTY_CYCLE);
    }
    else if (sbuf.indexOf("?") != -1)
    {
      PrintHelpStats();      
    }
   // else if (sbuf.indexOf("k") != -1)
   // {      
   //   Serial.println("Kickstart " );
   //   StartupPulses();
   // }
    else if (sbuf.indexOf("s") != -1)
    {
      if(curstate == eIdle)
      {
        curstate = eKickStart;  
        Serial.println("Starting motor " );
      }else
      {
        curstate = eIdle;
        Serial.println("Stopping Motor " );
        analogWrite(PIN_MOSFET, 0);
      }
      // start / stop  
    }
    sbuf = ""; // clear it  
  }
}

/*
This mode is to make sure the motor spins up.
There can be severa tings going on:
-The motor is already spinning and the user simply stopped and restarted
-The motor is at a dead stop (rpm == 0 for > X seconds)
-The motor is running in the reverse direction
  Could because of previous failure of kickstart?
This function is re-entrant and will execute if in kickstart mode
*/
void DoKickStart()
{
  if (curstate == eIdle)
    return;
    // so - anything could be happening here
    // we could be at a dead stop - we should check our rpm to see if we're at 0 rpm for multiple seconds
      // - send startup pulses
      // monitor to see if speed increases for X seconds
    // we could be spinning (in the right direction)
    // simply go to run mode
  if(curRPM == 0) // we're either in kickstart mode or run mode and we're at a dead stop and we're supposed to be running
    StartupPulses();

  if(curRPM > 500 && curstate == eKickStart)
  {
    curstate = eRun;
    Serial.println("Kickstart -> Run");
  }
    // check to exit kickstart mode
    // see if the current trend has been increasing over the past X seconds
    
}

/*This is a routine to get this thing up and spinning */
void StartupPulses()
{
  analogWrite(PIN_MOSFET, PWMLEVEL);
  delay(300)  ;
  analogWrite(PIN_MOSFET, 0);
  delay(400)  ;
//  analogWrite(PIN_MOSFET, PWMLEVEL);
//  delay(700)  ;
//  analogWrite(PIN_MOSFET, 0);
//  delay(100)  ;
}

void DoModeInput() // read the button
{
  int reading = digitalRead(PIN_BUTTON); //read the select button pin, if pressed will be low, if not pressed will be high
  if (reading != lastSelectButtonState)  //if the reading is not equal to the last state - set the last debounce time to current millis time
  {
    lastSelectDebounceTime = millis();
  }
  //check the difference between current time and last registered button press time, if it's greater than user defined delay then change the state as it's not a bounce
  if ((millis() - lastSelectDebounceTime) > DEBOUNCE_DELAY)
  {
    if (reading != SelectButtonState)  // check for change
    {
      SelectButtonState = reading;
      if (SelectButtonState == HIGH) // just transitioned high
      {
        switch(curstate)
        {
           case eIdle:// waiting to spin up - speed at 0
            curstate = eKickStart; 
            Serial.println("Idle -> eKickStart");
            StartupPulses();
            break;
           case eKickStart:// currently running, go to idle
            curstate = eIdle;
            analogWrite(PIN_MOSFET, 0); // shut down the motor
            Serial.println("KickStart -> Idle");
            break; 
           case eRun:// currently running, go to idle
            curstate = eIdle;
            analogWrite(PIN_MOSFET, 0); // shut down the motor
            Serial.println("Running -> Idle");
            break; 
        } 
      }
      else
      {
          // user let go of the mode button, end the test for long press
          //modepresstest = false;
      }
    }
  }
  //save the reading
  lastSelectButtonState = reading;
}
void SetLEDColor(int r,int g, int b)
{
    analogWrite(PIN_LED_RED, r);
    analogWrite(PIN_LED_GREEN, g);
    analogWrite(PIN_LED_BLUE, b);
}

void DoLEDOutput()
{
  switch(curstate)
  {
     case eIdle:// waiting to spin up - speed at 0
     SetLEDColor(255,0,0);
      break;
     case eRun:// spinning up to speed
     SetLEDColor(0,255,0);
      break; 
     case eKickStart: //
     SetLEDColor(0,0,255);
      break;
  }  
}
/*
// poll the hall sensor and get an estimate on RPM as well as triggering high and low
We need to: 
  - Poll the Hall Sensor, determine a new value
  - detect change in values
  - 
  Estimate the RPM
  - 
  we need a way to make the RPM go towards 0 when the state stops changing
Th current working theory here is that 
*/
void PollHall()
{
  unsigned long timenow = micros();
  static unsigned long lastlowstart_uS = 0;
  static unsigned int curstate = 0, laststate = 0; // say we're low  
  
  int val = analogRead(PIN_HALLSENSOR); // the hall sensor needed to be ground-biased through a resistor - otherwise we'd get floating values
  //int val = digitalRead(PIN_HALLSENSOR);
  //Serial.println(val);

  if(val < 128)
    curstate = 0;
  else
    curstate = 1;

  if(laststate != curstate) // something changed
  {
    if(laststate == 0) // we were at low, we're now going to high
    {
      //update the rpm counter 
      //determine the duration since the last
      lastperiod_duration_uS = timenow - lasthighstart_uS;
      double timediff_S = lastperiod_duration_uS /1000000.0;
      double freq = 1/timediff_S; // convert from time to frequency 
      curRPM = freq  * 60; // infer the RPM from the RPS frequency      
      curfrequency = freq; // in hz
      lasthighstart_uS = timenow; // now we're going high, mark the start
      triggered_this_cycle = 0; // clear the flag
      triggered_this_cycle2 = 0 ; // clear

    }
    else
    {
      lastlowstart_uS = micros();
    }
    laststate = curstate;
    laststatechangetime_uS = timenow;
  }
  if((timenow - laststatechangetime_uS) > 1000000)
    curRPM  = 0 ;
}
/*
So - We're attempting to determine the phase angle based on a few things
We know the time in uSeconds for the RPM - this is 'lastperiod_duration_uS'
We also know when the last period started 'lasthighstart_uS'
based on the time now, we can estimate from 0-100 where we are in the cycle
knowing that - we can use a phase offset angle to know when to trigger the next pulse
We should also determine the duration of the pulse
*/
/*
void DoublePulseAngle()
{
  double curphase = 0; // what the current phase angle is (0-100)
  unsigned long timenow = micros();
  unsigned long reltime_uS = timenow - lasthighstart_uS; // number of microseconds since the last period started
  if(lastperiod_duration_uS == 0)return; // avoid a divide by 0
  curphase = (double)((double)reltime_uS) / ((double)lastperiod_duration_uS);
  curphase *= 100; //scale to 0 -> 100

  if((curphase > TRIGGER_ANGLE) && (triggered_this_cycle == 0))
  {
    // if it's time to trigger
    digitalWrite(PIN_MOSFET, HIGH);
    pulse_end_time = timenow + (lastperiod_duration_uS * (((double)DUTY_CYCLE /2)/((double)100.0)));
    triggered_this_cycle = 1;
  }

  double trig2 = (double)(((int)TRIGGER_ANGLE * 2 ) % 100);
  if((curphase > trig2) && (triggered_this_cycle2 == 0))
  {
    // if it's time to trigger
    digitalWrite(PIN_MOSFET, HIGH);
    pulse_end_time = timenow + (lastperiod_duration_uS * (((double)DUTY_CYCLE /2)/((double)100.0)));
    triggered_this_cycle2 = 1;
  }
  //now we know where we are in the cycles
  // if the relative time now is greater than the previous cycle period - we start to get numbers greater than 1  
  if(timenow > pulse_end_time)
  {
    digitalWrite(PIN_MOSFET, LOW);
  }
}
*/
void PulseAngle()
{
  double curphase = 0; // what the current phase angle is (0-100)
  unsigned long timenow = micros(); // get the time now
  unsigned long reltime_uS = timenow - lasthighstart_uS; // number of microseconds since the last period started
  if(lastperiod_duration_uS == 0)return; // avoid a divide by 0
  curphase = (double)((double)reltime_uS) / ((double)lastperiod_duration_uS); // get the fraction portion of the time
  curphase *= 100;// scale to 0 -> 100

  if((curphase > TRIGGER_ANGLE) && (triggered_this_cycle == 0))
  {
    // if it's time to trigger
    analogWrite(PIN_MOSFET, PWMLEVEL);
    pulse_end_time = timenow + (((double)lastperiod_duration_uS) * (((double)DUTY_CYCLE)/((double)100.0)));
    triggered_this_cycle = 1;
  }
  //now we know where we are in the cycles
  // if the relative time now is greater than the previous cycle period - we start to get numbers greater than 1  
  if(timenow > pulse_end_time)
  {
    analogWrite(PIN_MOSFET, 0);
  }
}

void AddRPMValue(int val)
{
  rpmlog[rpmidx++] = val;
  if(rpmidx == NUMRPMLOG)
    rpmidx = 0;
}

bool IsRpmIncreasing()
{
  int idx = rpmidx;
  bool increase = false;
  for(int x = 0 ;x < 5; x ++)
  {    
    idx--;
    if(idx < 0) 
      idx = NUMRPMLOG -1;
  }
}

void loop() 
{
  DoLEDOutput();
  DoModeInput();
  DoSerialInput();
  DoKickStart();
  PollHall();
  if(curstate != eIdle)
    PulseAngle(); // pulse at the right time in the cycle

  // send periodic stats
  if(millis() > nextOneSecondTimer)
  {
    nextOneSecondTimer = millis() + 1000;
    Serial.println(curRPM);
    AddRPMValue(curRPM);
  }
}
