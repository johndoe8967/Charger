/*
  Ladegerät für NiMh, LiPo cells with various number of cells and capacity
  3 differnt type of charging
  - fast NiMh ?
  - fast LiPo ConstantCurrent - ConstantVoltage
  - slow time based


  The circuit:
 * cell Current Input       at pin A0
 * cell voltage Input       at pin A1
 * charge current output    at pin 9
 * Button select            at pin 6
 * Button +                 at pin 7
 * Button -                 at pin 8
 * LCD RS pin to digital      pin 12
 * LCD Enable pin to digital  pin 11
 * LCD D4 pin to digital      pin 5
 * LCD D5 pin to digital      pin 4
 * LCD D6 pin to digital      pin 3
 * LCD D7 pin to digital      pin 2  
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

// initialize the LCD library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// definition of the analog IO
// ADC values will be filtered by SmoothADC (best result if 16 samples are filtered instead of 4 in the library)
const int refout = 9;
const int sensorPinI = A0;
const int sensorPinU = A1;
SmoothADC    ADC_0;        // SmoothADC instance for voltage
SmoothADC    ADC_1;        // SmoothADC instance for current

// conversion factors for ADC increments to mA and mV
const float mAOutPerInc = 4.18;       // 8bit PWM output -> 256*4,18 = 1070mA
const float mVInPerInc = 22.146;       // (22,1=Spannungsteiler*Uref);
const float mAInPerInc = 1.08;

// declaration of analog variables
  int cellVoltage  = 0;
  int cellCurrent  = 0;
  unsigned long cellmAs = 0;
  int refoutvalue = 100/mAOutPerInc;// (I[mA]/3,94)  


// initialize charge current outut and limit
const int limitCurrent = 1000;
      int chargeCurrent = 0;

// initialize charge run time and limit
const int limitRuntime = 16*60;
      int maxRuntime = 4*60;
      int runtimeMinutes = 0;
      
// initialize LiPo specific charge limits
const int maxCellVoltageLiPo = 4230;
const int maxConstCurrentVoltageLiPo = 4200;

enum LiPoState {
  CHECK=0,
  Cc,
  CC,
  CV,
  FULL,
  WAIT
} actLiPoState = WAIT;


// indicator if cell is connected and charge is in progress
  bool charging = false;

// display and menu related variables
enum MenuState {
  Type=0,
  Current=1,
  Time=2,
  LASTMENUSTATE=3
} menuState = Type;

enum CellTypes {
  NiCd=0, NiMh=1, LiPo=2, LASTCellTypesTATE=3
} actType = NiCd;

const char *message = 0;        // pointer to const string for message display

const int fractionOfSecond = 2;


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
  // read buttons
  // pressed button pulles down to gnd
  buttonMode = !digitalRead(buttonModePin);
  buttonInc = !digitalRead(buttonIncPin);
  buttonDec = !digitalRead(buttonDecPin);

  // detect if any button is pressed to clear message from display
  if (buttonMode || buttonInc || buttonDec) {
    if (message != 0) {
      message = 0;
      return;             // ignore button for 1 cycle to reset message without value change    
    }
  }

  // buttons should only work if cell is not charging (and the menu is visible)
  if (!charging) {
    if (buttonMode == true) {                                 // process mode button as loop of modes
      menuState=(MenuState)((int)menuState+1);                // increment mode
      if (menuState == LASTMENUSTATE) menuState=Type;         // at the last mode set first mode
    }
  
    if (buttonInc == true) {                                  // process increment button for each mode
      switch (menuState) {
        case Type:
          if (actType < (CellTypes)(LASTCellTypesTATE-1)) {   // increment only of not already at the last cell type
            actType =(CellTypes)((int)actType+1);
          }
          break;
        case Current:
          chargeCurrent = chargeCurrent+10;                   // increment charge current and limit to maximum
          if (chargeCurrent > limitCurrent) chargeCurrent = limitCurrent;
          break;
        case Time:
          maxRuntime = maxRuntime+15;                                       // increment charge runtime and limit to maximum
          if (maxRuntime > limitRuntime) maxRuntime = limitRuntime;
          break;
        default:
          break;
      }
    }
    
    if (buttonDec == true) {                                  // process decrement button for each mode
      switch (menuState) {
        case Type:
          if (actType > NiCd) {                               // decrement only of not already at the first cell type
            actType =(CellTypes)((int)actType-1);
          }
          break;
        case Current:
          chargeCurrent = chargeCurrent-10;                   // decrement charge current and limit to positive values
          if (chargeCurrent < 0) chargeCurrent = 0;
          break;
        case Time:
          maxRuntime = maxRuntime-15;                                       // decrement charge runtime and limit to positive values
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
  lcd.begin(16, 2);                             // set up the LCD's number of columns and rows:

  analogReference(INTERNAL);                    //interne Referenz 1,082V

  ADC_0.init(sensorPinU, TB_MS, 50);            // Init ADC0 attached to A0 with a 50ms acquisition period
  if (ADC_0.isDisabled()) { ADC_0.enable(); }
  ADC_1.init(sensorPinI, TB_MS, 50);            // Init ADC0 attached to A0 with a 50ms acquisition period
  if (ADC_1.isDisabled()) { ADC_1.enable(); }
  
  pinMode(refout, OUTPUT);
  
  initButtons();
  printSplash();
}

//******************************************
// print message on first row of display
//******************************************
void printMessage() {
static int messagedelay=0;
  if (message != 0) {                         // only if message is not empty
    messagedelay++;
    if ((messagedelay/fractionOfSecond)%2) {  // blink message with 0,5Hz 
      // clear first row of display
      lcd.setCursor(0, 0);
      lcd.print("                ");
      lcd.setCursor(0, 0);
      lcd.print(message);
    }
  }
}
//******************************************
// print splash
//******************************************
void printSplash () {
  lcd.setCursor(0, 0);
  lcd.print("Batteryloader   ");
  lcd.setCursor(0, 1);
  lcd.print("max 19Vac V01.00");
}

//******************************************
// print status e.g. during charging
//******************************************
const char LiPoStateString[6][3] = {"Ch","Cc","CC","CV","FU","Wa"};
void printStatus () {
  
  printTime(0, 0);
  
  lcd.setCursor(9, 0);
  float Ah = (float)cellmAs / 3600000.0;
  lcd.print(Ah,1);
  lcd.print("mAh    ");
  
  lcd.setCursor(0, 1);

  if (cellVoltage < 100) lcd.print(" ");    // align cellvoltage
  if (cellVoltage < 1000) lcd.print(" ");
  if (cellVoltage < 10000) lcd.print(" "); 
  lcd.print(cellVoltage);                   //        5 char
  lcd.print("mV");                          //        2 char

  if (cellCurrent < 10) lcd.print(" ");     // align cellcurrent
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
const char CellTypestring[3][5] = {"NiCd","NiMh","LiPo"};
void printMenu (MenuState menuState) {
  lcd.setCursor(0, 0);
  lcd.print("Typ, Strom, Zeit");              // complete reprint 1st line of menu
  lcd.setCursor(0, 1);
  lcd.print("                ");              // clear 2nd line of menu
  lcd.setCursor(0, 1);
  lcd.print(CellTypestring[actType]);         // print celltype
  
  lcd.setCursor(5, 1);
  if (chargeCurrent < 10) lcd.print(" ");     // align chargecurrent
  if (chargeCurrent < 100) lcd.print(" ");
  if (chargeCurrent < 1000) lcd.print(" ");
  if (chargeCurrent < 10000) lcd.print(" "); 
  lcd.print(chargeCurrent);                   // print chargecurrent
  
  lcd.setCursor(12, 1); 
  if (maxRuntime < 100) lcd.print(" ");       // align max runtime
  if (maxRuntime < 1000) lcd.print(" ");
  lcd.print(maxRuntime);                      // print max runtime

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
static unsigned long lastMeasureTime;
  unsigned long actMeasureTime = millis();
  unsigned long elapsedTime = actMeasureTime - lastMeasureTime;
  lastMeasureTime = actMeasureTime;
  
  int sensorValueU = ADC_0.getADCVal();                   // get smoothed ADC values
  int sensorValueI = ADC_1.getADCVal();
  cellCurrent = sensorValueI*mAInPerInc;                  // increments to mA
  cellVoltage = int(sensorValueU*mVInPerInc-cellCurrent); // increments to mV
  cellmAs += cellCurrent * elapsedTime;                   // calculate mA seconds
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
  runtimeMinutes = 0;
  cellmAs = 0;
}
//******************************************
// calculate charge current 
//  each type has an individual calculation
//******************************************
void calcChargeCurrent() {
static int voltageDetectionCounter=0;  
  switch (actType) {
    case NiCd:
      refoutvalue = chargeCurrent/mAOutPerInc;            // constant charge current during complete time
      break;
    case NiMh:
      refoutvalue = chargeCurrent/mAOutPerInc;            // constant charge current during complete time
      break;
    case LiPo:
      switch (actLiPoState) {
        case CHECK:
          refoutvalue = (chargeCurrent/mAOutPerInc)/10;   // min charge current is 10% of rated charge current
          delayCounter++;
          if (delayCounter >= fractionOfSecond*10) {      // delay for 10 seconds
            delayCounter = 0;   
            
            if (cellVoltage < maxConstCurrentVoltageLiPo) {       // cell is empty -> switch to constant current
              actLiPoState = Cc;
            } else if (cellVoltage < maxCellVoltageLiPo) {        // cell nearly full -> switch to constant voltage
              actLiPoState = CV;
            } else {                                              // cell full -> switch end of charge
              actLiPoState = FULL;
            }         
          }
          break;

        case Cc:    // initialice Constant Current
          refoutvalue = chargeCurrent/mAOutPerInc;          // constant current als long as voltage is lower than the limit   
          actLiPoState = CC;     
          break;
        case CC:
          // closed loop current control
          if (cellCurrent < (chargeCurrent-5)) {            // increment current output if measurement is below charge current
            refoutvalue++;
          }
          if (cellCurrent > (chargeCurrent+5)) {            // decrement current output if measurement is above charge current 
            refoutvalue--;
          }

          if ((refoutvalue*mAOutPerInc - chargeCurrent) > 200) {  // terminate charge is set current is 200mA above charge current
            refoutvalue = 0;                                      // switch of current output
            message = "Current ERROR   ";                         // set message for display
            actLiPoState = WAIT;                                  // switch to WAIT state
          }
          
          if (cellVoltage > maxConstCurrentVoltageLiPo) {         // detect state change because cellVoltage above constant current limit
            voltageDetectionCounter++;                            // delay state change 10s 
            if (voltageDetectionCounter > (10*fractionOfSecond)) {
              actLiPoState = CV;                                  // next state constant voltage
              voltageDetectionCounter = 0;                        // reset delay counter for next usage
            }
          }else {                                                 // reset state change because voltage to low again
            voltageDetectionCounter = 0;
          }          
          break;
        case CV:
          // regulate cell voltage by adjusting the current
          if (cellVoltage > (maxConstCurrentVoltageLiPo + 10)) {
            refoutvalue--;                                        // reduce charge current
            if (refoutvalue < 0) refoutvalue=0;                   // prevent underrun of current
          }
          if (cellVoltage < (maxConstCurrentVoltageLiPo - 10)) {
            refoutvalue++;                                        // increase charge current
            if (refoutvalue > chargeCurrent/mAOutPerInc) {        // prevent higher currents than allowed
              refoutvalue = chargeCurrent/mAOutPerInc;
            }
          }
          if (refoutvalue < ((chargeCurrent/mAOutPerInc)/10)) {   // stop charging at 10% of initial charge current
            actLiPoState = FULL;
          }
          if (cellVoltage > maxCellVoltageLiPo) {                 // detect end of charge bewcause cellVoltage above max cell voltage
            voltageDetectionCounter++;                            // delay state change 10s 
            if (voltageDetectionCounter > (10*fractionOfSecond)) {
              actLiPoState = FULL;                                // next state FULL
              voltageDetectionCounter = 0;                        // reset delay counter for next usage          
            }
          }else {                                                 // reset state change because voltage to low again
            voltageDetectionCounter = 0;          
          }
          break;
        case FULL:
          refoutvalue = 0;                                        // switch of current
          message = "LiPo FULL       ";                           // set message for display
          actLiPoState = WAIT;                                    // next state WAIT
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
// init time measument with actual millis
//******************************************
void initRunTime() {
  previousTime = millis();
}
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
  
  if (hours<10) lcd.print("0");     // print hours with 2 decimal places
  lcd.print(hours);
  
  lcd.print(":"); 
  
  if (minutes<10) lcd.print("0");   // print minutes with 2 decimal places  
  lcd.print(minutes);
  
  lcd.print(":"); 
  
  if (seconds<10) lcd.print("0");   // print seconds with 2 decimal places  
  lcd.print(seconds);
  lcd.print(" ");
}

//******************************************
// main loop
//******************************************
void loop() {
static int count = 0;
static int delayMenu = 5*fractionOfSecond;            // delay 5s to show splash
  ADC_0.serviceADCPin();
  ADC_1.serviceADCPin();

  count++;
  if ((count%10)==0) {
    getChargeState();                 // read analog values and calculate cell values
    calcChargeCurrent();              // calculate charge current and state
    setChargeCurrent();               // output charge current
  
    processButtons();                 // read and process buttons
  
    if (cellCurrent == 0) {           // check charging depending on current flow to detect a cell
      charging = false;
    } else {
      if (charging == false) {        // check for start of charging positive edge
        initCharging();               // init charging state
        charging = true;              
      }
    }

    if (delayMenu > 0) {
      delayMenu--;
      initRunTime();
    } else {
      if(charging == true)            // show status or menu depending on charging state 
      {
        printStatus();
        calcRunTime();
      } else {
        if (message != 0) {
          printStatus();        
        } else {
          printMenu(menuState);
          initRunTime();
        }
      }    
    }
    printMessage();    
  }
  
  delay(1000/fractionOfSecond/10);     
}
