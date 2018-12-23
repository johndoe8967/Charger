/*
  Ladegerät für NiMh, LiPo cells with various number of cells and capacity
  3 differnt type of charging
  - fast NiMh ?
  - fast LiPo ConstantCurrent - ConstantVoltage
  - slow time based


  The circuit:
 * Button select  at pin ?
 * Button +       at pin ?
 * Button -       at pin ?
 * LCD RS pin to digital pin 12
 * LCD Enable pin to digital pin 11
 * LCD D4 pin to digital pin 5
 * LCD D5 pin to digital pin 4
 * LCD D6 pin to digital pin 3
 * LCD D7 pin to digital pin 2
 * LCD R/W pin to ground
 * LCD VSS pin to ground
 * LCD VCC pin to 5V
 * 10K resistor:
 * ends to +5V and ground
 * wiper to LCD VO pin (pin 3)
*/

// include the library code:
#include <LiquidCrystal.h>
#include <SmoothADC.h>

// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


const int refout = 9;
const int sensorPinI = A0;
const int sensorPinU = A1;
SmoothADC    ADC_0;        // SmoothADC instance for voltage
SmoothADC    ADC_1;        // SmoothADC instance for current


const float mAPerinc = 4.18;
const float n = 22.24;      // (22,1=Spannungsteiler*Uref);
const float a = 1.08;

enum MenuState {
  Type=0,
  Current=1,
  Time=2,
  LASTMENUSTATE=3
};
  MenuState menuState = Type;

enum Types {
  NiCd=0, NiMh=1, LiPo=2, LASTTYPESTATE=3
};

  Types actType = NiCd;
const int limitCurrent = 1000;
      int chargeCurrent = 0;

const int limitRuntime = 16*60;
      int maxRuntime = 4*60;
      
  bool charging = false;
  
  int refoutvalue = 600/mAPerinc;// (I[mA]/3,94)  
  
const int maxCellVoltageLiPo = 4230;
const int maxConstCurrentVoltageLiPo = 4200;
      int cellVoltage  = 0;
      int cellCurrent  = 0;

enum LiPoState {
  CHECK=0,
  Cc,
  CC,
  CV,
  FULL,
  WAIT
  };

static LiPoState actLiPoState = WAIT;
      
  int runtimeMinutes = 0;
const int fractionOfSecond = 2;

const char *message = 0;

//******************************************
//  button related functions and variables
//******************************************
const int buttonModePin = 6;    
     bool buttonMode = false;
const int buttonIncPin = 7;     
     bool buttonInc = false;
const int buttonDecPin = 8;     
     bool buttonDec = false;

//******************************************
//  initButtons to input
//******************************************
void initButtons() {
  pinMode(buttonModePin, INPUT);  
  pinMode(buttonIncPin, INPUT);
  pinMode(buttonDecPin, INPUT);
}
//******************************************
//  processButtons read the pins 
//    and calc menuState, chargeCurrent and maxRuntime
//******************************************
void processButtons () {
  buttonMode = !digitalRead(buttonModePin);
  buttonInc = !digitalRead(buttonIncPin);
  buttonDec = !digitalRead(buttonDecPin);

  if (buttonMode || buttonInc || buttonDec) {
    message = 0;
  }
  
  if (!charging) {
    if (buttonMode == true) {
      menuState=(MenuState)((int)menuState+1);
      if (menuState == LASTMENUSTATE) menuState=Type;
    }
  
    if (buttonInc == true) {
      switch (menuState) {
        case Type:
          if (actType < LiPo) {
            actType =(Types)((int)actType+1);
          }
          break;
        case Current:
          chargeCurrent = chargeCurrent+10;
          if (chargeCurrent > limitCurrent) chargeCurrent = limitCurrent;
          break;
        case Time:
          maxRuntime++;
          if (maxRuntime > limitRuntime) maxRuntime = limitRuntime;
          break;
        default:
          break;
      }
    }
    if (buttonDec == true) {
      switch (menuState) {
        case Type:
          if (actType > NiCd) {
            actType =(Types)((int)actType-1);
          }
          break;
        case Current:
          chargeCurrent = chargeCurrent-10;
          if (chargeCurrent < 0) chargeCurrent = 0;
          break;
        case Time:
          maxRuntime--;
          if (maxRuntime < 0) maxRuntime = 0;
          break;
        default:
          break;
      }
    }
  }

}

//******************************************
// Setup all IOs and LCD
//******************************************
void setup() {
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);

  //interne Referenz 1,082V
  analogReference(INTERNAL);

  ADC_0.init(sensorPinU, TB_MS, 50);  // Init ADC0 attached to A0 with a 50ms acquisition period
  if (ADC_0.isDisabled()) { ADC_0.enable(); }
  ADC_1.init(sensorPinI, TB_MS, 50);  // Init ADC0 attached to A0 with a 50ms acquisition period
  if (ADC_1.isDisabled()) { ADC_1.enable(); }
  
  pinMode(refout, OUTPUT);
  
  initButtons();
}

//******************************************
// print message
//******************************************
void printMessage() {
static int messagedelay=0;
  messagedelay++;
  if ((messagedelay/fractionOfSecond)%2) {  
    if (message != 0) {
      lcd.setCursor(0, 0);
      lcd.print("                ");
      lcd.setCursor(0, 0);
      lcd.print(message);
    }
  }
}
//******************************************
// print status e.g. during charging
//******************************************
const char LiPoStateString[6][3] = {"Ch","Cc","CC","CV","FU","Wa"};
void printStatus () {
  
  printTime(0, 0);    
  lcd.setCursor(0, 1);

  if (cellVoltage < 100) lcd.print(" ");
  if (cellVoltage < 1000) lcd.print(" ");
  if (cellVoltage < 10000) lcd.print(" "); 
  lcd.print(cellVoltage);                   //        5 char
  lcd.print("mV");                          //        2 char

  if (cellCurrent < 10) lcd.print(" ");
  if (cellCurrent < 100) lcd.print(" ");
  if (cellCurrent < 1000) lcd.print(" ");
  lcd.print(cellCurrent);                   //        4 char
  lcd.print("mA ");                         //        3 char
  lcd.print(LiPoStateString[actLiPoState]); //        2 char
  lcd.noBlink();
}

//******************************************
// print menu
//******************************************
const char typeString[3][5] = {"NiCd","NiMh","LiPo"};
void printMenu (MenuState menuState) {
  lcd.setCursor(0, 0);
  lcd.print("Typ, Strom, Zeit");
  lcd.setCursor(0, 1);
  lcd.print(typeString[actType]);
  lcd.print("      ");
  lcd.setCursor(5, 1);
  lcd.print(chargeCurrent);
  lcd.setCursor(12, 1);
  lcd.print("    ");
  lcd.setCursor(12, 1);
  lcd.print(maxRuntime);  

  // set blinking cursor at the end of the value
  int cursorpos = 0;
  switch (menuState) {
    case Type:
      cursorpos = 3;
      break;
    case Current:
      cursorpos = 10;
      break;
    case Time:
      cursorpos = 15;
      break;
    default:
      break;
  }
  lcd.setCursor(cursorpos,1);
  lcd.blink();
}


//******************************************
// Charge related functions and variables
//******************************************
static int delayCounter = 0;

//******************************************
// measure voltage and current
// calculate cellvoltage by subtracting drop on current measurement
//******************************************
void getChargeState () {
  int sensorValueU = ADC_0.getADCVal();
  int sensorValueI = ADC_1.getADCVal();
  cellCurrent = sensorValueI*a;
  cellVoltage = int(sensorValueU*n-cellCurrent);
}

//******************************************
// set current 
//******************************************
void setChargeCurrent() {
  analogWrite (refout,refoutvalue);  
}

//******************************************
// initialize charging 
//******************************************
void initCharging() {
  actLiPoState=CHECK;
  delayCounter = 0;  
}
//******************************************
// calculate charge current 
//  each type has an individual calculation
//******************************************
void calcChargeCurrent() {
  switch (actType) {
    case NiCd:
      refoutvalue = chargeCurrent/mAPerinc;          // constant charge current during complete time
      break;
    case NiMh:
      refoutvalue = chargeCurrent/mAPerinc;          // constant charge current during complete time
      break;
    case LiPo:
      switch (actLiPoState) {
        case CHECK:
          refoutvalue = (chargeCurrent/mAPerinc)/10;  // min charge current is 10% of rated charge current
          delayCounter++;
          if (delayCounter >= fractionOfSecond*10) {  // delay for 10 seconds
            delayCounter = 0;   
            if (cellVoltage < maxConstCurrentVoltageLiPo) {
              actLiPoState = Cc;
            } else if (cellVoltage < maxCellVoltageLiPo) {
              actLiPoState = CV;
            } else {
              actLiPoState = FULL;
            }         
          }
          break;

        case Cc:    // initialice Constant Current
          refoutvalue = chargeCurrent/mAPerinc;         // constant current als long as voltage is lower than the limit        
          break;
        case CC:
          if (cellCurrent < (chargeCurrent-10)) {
            refoutvalue++;
          }
          if (cellCurrent > (chargeCurrent+10)) {
            refoutvalue--;
          }
          if (cellVoltage > maxConstCurrentVoltageLiPo) {   
            actLiPoState = CV;
          }          
          break;
        case CV:
          // regulate cell voltage by adjusting the current
          if (cellVoltage > (maxConstCurrentVoltageLiPo + 10)) {
            refoutvalue--;                              // reduce charge current
            if (refoutvalue < 0) refoutvalue=0;         // prevent underrun of current
          }
          if (cellVoltage < (maxConstCurrentVoltageLiPo - 10)) {
            refoutvalue++;                              // increase charge current
            if (refoutvalue > chargeCurrent/mAPerinc) { // prevent higher currents than allowed
              refoutvalue = chargeCurrent/mAPerinc;
            }
          }
          if (refoutvalue < ((chargeCurrent/mAPerinc)/10)) { // stop charging at 10% of initial charge current
            actLiPoState = FULL;
          }
          if (cellVoltage > maxCellVoltageLiPo) {       // detect end of charge
            actLiPoState = FULL;
          }
          break;
        case FULL:
          refoutvalue = 0;
          message = "LiPo FULL       ";
          actLiPoState = WAIT;
          break;
        case WAIT:
          break;
      }
      break;
    default:
      break;
  }
  if (runtimeMinutes >= maxRuntime) 
  {
    refoutvalue = 0;
  }
}


//******************************************
// Runtime related variables and functions
//******************************************
unsigned long previousTime = 0;
byte seconds ;
byte minutes ;
byte hours ;

//******************************************
// calcRunTime in seconds, minutes and hours
//******************************************
void calcRunTime() {
  if (millis() >= (previousTime))           // check if one second elapsed
  {
     previousTime = previousTime + 1000;    // calculate timestamp for next second
     seconds++;
     if (seconds == 60) {
        seconds = 0;
        minutes++;
        runtimeMinutes++;
      }
     if (minutes == 60) {
        minutes = 0;
        hours++;
      }
     if (hours == 24) {
        hours = 0;
      }  
   }
}
//******************************************
// reset runtime to zero 
//******************************************
void clearRunTime() {
  seconds = 0;
  minutes = 0;
  hours = 0;
}
//******************************************
// print runtime on first line of display
//******************************************
void printTime(int col, int row) {
  lcd.setCursor(col, row);
  lcd.print("RunTime ");
  
  if (hours<10) lcd.print("0");     // print hours with 2 decimal places
  lcd.print(hours);
  
  lcd.print(":"); 
  
  if (minutes<10) lcd.print("0");   // print minutes with 2 decimal places  
  lcd.print(minutes);
  
  lcd.print(":"); 
  
  if (seconds<10) lcd.print("0");   // print seconds with 2 decimal places  
  lcd.print(seconds);
}

//******************************************
// main loop
//******************************************
void loop() {
static int count = 0;
  ADC_0.serviceADCPin();
  ADC_1.serviceADCPin();

  count++;
  if ((count%10)==0) {
    getChargeState();                 // read analog values and calculate cell values
    calcChargeCurrent();              // calculate charge current and state
    setChargeCurrent();               // output charge current
  
    processButtons();                 // read and process buttons
  
    if (cellCurrent == 0) {          // check charging depending on current flow to detect a cell
      charging = false;
    } else {
      if (charging == false) {        // check for start of charging positive edge
        initCharging();               // init charging state
        charging = true;              
      }
    }
    
    if(charging == true)              // show status or menu depending on charging state 
    {
      printStatus();
      calcRunTime();
    } else {
      printMenu(menuState);
    }
    printMessage();    
  }
  
  delay(1000/fractionOfSecond/10);     
}
