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




// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


const int refout = 9;
const int sensorPinI = A0;
const int sensorPinU = A1;

const float mAPerinc = 3.94;
const float n = 22.1;// (22,1=Spannungsteiler*Uref);
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
  int sensorValueI = 0;
  int sensorValueU = 0;
  
const int checkCurrentLiPo = 10;
const int maxCellVoltageLiPo = 4200;
const int maxConstCurrentVoltageLiPo = 4100;
      int cellVoltage  = 0;

  int runtimeMinutes = 0;
  
  int i = 0;

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
//  getButtons read the pins 
//    and calc menuState, chargeCurrent and maxRuntime
//******************************************
void getButtons () {
  buttonMode = !digitalRead(buttonModePin);
  buttonInc = !digitalRead(buttonIncPin);
  buttonDec = !digitalRead(buttonDecPin);
  
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

//******************************************
// Setup all IOs and LCD
//******************************************
void setup() {
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);

  //interne Referenz 1,082V
  analogReference(INTERNAL);
  pinMode(refout, OUTPUT);
  
  initButtons();
}

//******************************************
// print status e.g. during charging
//******************************************
void printStatus () {
  lcd.noBlink();
  printTime(0, 0);
  
  lcd.setCursor(0, 1);
  lcd.print(cellVoltage);
  lcd.print("mV  ");
  lcd.print(int(sensorValueI*a));
  lcd.print("mA  ");
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
enum LiPoState {
  CHECK=0,
  CC,
  CV,
  FULL
  };
static LiPoState actLiPoState;
static int delayCounter = 0;

//******************************************
// measure voltage and current
// improve accuracy by measuring 10 times and average it
// calculate cellvoltage by subtracting drop on current measurement
//******************************************
void getChargeState () {
  sensorValueU = 0;
  sensorValueI = 0;
  for (int i=0; i <= 9; i++)
  {
    sensorValueU = (sensorValueU + analogRead(sensorPinU));
    sensorValueI = (sensorValueI + analogRead(sensorPinI));
  }  
  sensorValueU = (sensorValueU)/10;
  sensorValueI = (sensorValueI)/10;
  cellVoltage = int(sensorValueU*n-sensorValueI*a);
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
          refoutvalue = checkCurrentLiPo/mAPerinc;
          delayCounter++;
          if (delayCounter >= 40) {
            delayCounter = 0;   
            if (cellVoltage < maxConstCurrentVoltageLiPo) {
              actLiPoState = CC;
            }
            if (cellVoltage < maxCellVoltageLiPo) {
              actLiPoState = CV;
            } else {
              actLiPoState = FULL;
            }         
          }
          break;
        case CC:
          refoutvalue = chargeCurrent/mAPerinc;        // constant current als long as voltage is lower than the limit
          if (cellVoltage > maxConstCurrentVoltageLiPo) {   
            actLiPoState = CV;
          }          
          break;
        case CV:
          if (cellVoltage > (maxConstCurrentVoltageLiPo + 10)) {
            refoutvalue--;                      // reduce charge current
            if (refoutvalue < 0)
            {
              refoutvalue=0;
            }
          }
          if (cellVoltage < (maxConstCurrentVoltageLiPo - 10)) {
            refoutvalue++;
            if (refoutvalue > chargeCurrent/mAPerinc) {
              refoutvalue = chargeCurrent/mAPerinc;
            }
          }
          if (cellVoltage > maxCellVoltageLiPo) {
            actLiPoState = FULL;
          }
          break;
        case FULL:
          refoutvalue = 0;
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
  if (millis() >= (previousTime))
  {
     previousTime = previousTime + 1000;  // use 100000 for uS
     seconds = seconds +1;
     if (seconds == 60)
      {
        seconds = 0;
        minutes = minutes +1;
        runtimeMinutes += 1;
      }
     if (minutes == 60)
      {
        minutes = 0;
        hours = hours +1;
      }
     if (hours == 24)
      {
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
  if (hours<10)
  {
    lcd.print("0");  
  }
  lcd.print(hours);
  lcd.print(":"); 
  if (minutes<10)
  {
    lcd.print("0");  
  }
  lcd.print(minutes);
  lcd.print(":"); 
  if (seconds<10)
  {
    lcd.print("0");  
  }
  lcd.print(seconds);
}

//******************************************
// main loop
//******************************************
void loop() {

  getChargeState();
  calcChargeCurrent();
  setChargeCurrent();

  getButtons();

  if (sensorValueI == 0) {
    charging = false;
  } else {
    if (charging == false) {
      initCharging();
      charging = true;
    }
  }
  
  if(charging == true) 
  {
    printStatus();
    calcRunTime();
  } else {
    printMenu(menuState);
  }
  
  
  delay(500);
}
