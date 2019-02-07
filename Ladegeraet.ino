/*
  Ladegerät für NiMh, LiPo cells with various number of cells and capacity
  3 differnt type of charging
  - fast NiMh ?
  - fast LiPo ConstantCurrent - ConstantVoltage
  - slow time based


  The circuit:
 * cell Current Input       at pin A0
 * cell voltage Input       at pin A1
 * cell temperature Input   at pin A2
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
const int sensorTemp = A2;
const int sensorPinI_discharge = A3;
SmoothADC ADC_0;        // SmoothADC instance for voltage
SmoothADC ADC_1;        // SmoothADC instance for current
SmoothADC ADC_2;        // SmoothADC instance for temperature
SmoothADC ADC_3;        // SmoothADC instance for discharge_current

// conversion factors for ADC increments to mA and mV
const float mAOutPerInc = 4.18;       // 8bit PWM output -> 256*4,18 = 1070mA
const float mVInPerInc = 15.879;       // (22,146=Spannungsteiler*Uref);
const float mAInPerInc = 1.08;
#define logSensor
#ifdef logSensor
const float CPerInc = -30.8;          //43kOhm-> -31.4
const float COffset = 232.7;          //43kOhm-> 238.6
#else
const float CPerInc = 0.547;          
const float COffset = -251.7;         
#endif
const float tempFilter = 0.01;

// declaration of analog variables
  int cellVoltage  = 0;
  int cellCurrent  = 0;
  bool dischargeSwitch;
#ifdef measureRI
  float cellRI = 0;
#endif
  float cellTemperature = 0.0;
  float cellTempFiltered = 0.0;
  float cellTempSlope = 0.0;
  float maxCellTempSlope = 0.0;
  unsigned long long cellmAs = 0;
const int batteryDetectCurrent = 100;
  int refoutvalue = batteryDetectCurrent/mAOutPerInc;// (I[mA]/3,94)  

bool measureCellRI=false;

// initialize charge current outut and limit
const int limitCurrent = 1000;
      int chargeCurrent = 0;
      int minCellVoltageDischarge = 2850;
const int limitDischargeVoltage = 15000;

// initialize charge run time and limit
const int limitRuntime = 16*60;
      int maxRuntime = 7*60;
      int runtimeMinutes = 0;
      
// initialize LiPo specific charge limits
const int maxCellVoltageLiPo = 4240;
const int maxConstCurrentVoltageLiPo = 4180;

enum LiPoState {
  CHECK=0,
  Cc,
  CC,
  CV,
  FULL,
  WAIT
} actChargeState = WAIT;


// indicator if cell is connected and charge is in progress
  bool charging = false;

// display and menu related variables
enum MenuState {
  Type=0,
  Current=1,
  Time=2,
  Voltage=3,
  LASTMENUSTATE=4
} menuState = Type;

enum CellTypes {
  NiCd=0, NiMh=1, LiPo=2, Discharge=3, LASTCellTypesTATE=4
} actType = NiCd;

const char *message = 0;        // pointer to const string for message display

const int fractionOfSecond = 2;

static int slopeDetectionCounter=0;

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
      refoutvalue = batteryDetectCurrent/mAOutPerInc;
      return;             // ignore button for 1 cycle to reset message without value change    
    }
  }

  // buttons should only work if cell is not charging (and the menu is visible)
  if (!charging) {
    if (buttonMode == true) {                                 // process mode button as loop of modes
      menuState=(MenuState)((int)menuState+1);                // increment mode
      if (actType == Discharge) {                             // if discharge mode then skip time entry
        if (menuState == Time) {
          menuState = (MenuState)((int)menuState+1);
        } 
      } else {
        if (menuState == Voltage) {                           // if not discharge then skip voltage entry
          menuState = (MenuState)((int)menuState+1);
        }
      }
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
          if (chargeCurrent >= 100) {
            chargeCurrent = chargeCurrent+100;                  // increment charge current and limit to maximum            
          } else {
            chargeCurrent = chargeCurrent+10;                   // increment charge current and limit to maximum
          }
          if (chargeCurrent > limitCurrent) chargeCurrent = limitCurrent;
          break;
        case Time:
          maxRuntime = maxRuntime+15;                            // increment charge runtime and limit to maximum
          if (maxRuntime > limitRuntime) maxRuntime = limitRuntime;
          break;
        case Voltage:
          minCellVoltageDischarge = minCellVoltageDischarge+100;  // increment discharge voltage and limit to maximum
          if (minCellVoltageDischarge > limitDischargeVoltage) minCellVoltageDischarge = limitDischargeVoltage;
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
          if (chargeCurrent > 100) {
            chargeCurrent = chargeCurrent-100;                   // decrement charge current and limit to positive values            
          } else {
            chargeCurrent = chargeCurrent-10;                   // decrement charge current and limit to positive values
          }
          if (chargeCurrent < 0) chargeCurrent = 0;
          break;
        case Time:
          maxRuntime = maxRuntime-15;                                       // decrement charge runtime and limit to positive values
          if (maxRuntime < 0) maxRuntime = 0;
          break;
        case Voltage:
          minCellVoltageDischarge = minCellVoltageDischarge-100;  // increment discharge voltage and limit to maximum
          if (minCellVoltageDischarge < 0) minCellVoltageDischarge = 0;
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
  ADC_1.init(sensorPinI, TB_MS, 50);            // Init ADC1 attached to A1 with a 50ms acquisition period
  if (ADC_1.isDisabled()) { ADC_1.enable(); }
  ADC_2.init(sensorTemp, TB_MS, 50);            // Init ADC2 attached to A2 with a 50ms acquisition period
  if (ADC_2.isDisabled()) { ADC_2.enable(); }
  ADC_3.init(sensorPinI_discharge, TB_MS, 50);  // Init ADC3 attached to A3 with a 50ms acquisition period
  if (ADC_3.isDisabled()) { ADC_3.enable(); }
  pinMode(refout, OUTPUT);
  
  initButtons();
  printSplash();
  Serial.begin(9600);
}

//******************************************
// print message on first row of display
//******************************************
void printMessage() {
static int messagedelay=0;
  if (message != 0) {                         // only if message is not empty
    messagedelay++;
    if ((messagedelay/fractionOfSecond*2)%(fractionOfSecond*2)) {  // blink message with 0,5Hz 
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
  lcd.print("max 19Vac V01.03");
}

//******************************************
// print status e.g. during charging
//******************************************
const char chargeStateString[6][3] = {"Ch","Cc","CC","CV","FU","Wa"};
void printStatus () {
static int counter;
  counter++;
  
  if (((counter/(fractionOfSecond*2)) % (fractionOfSecond*2)) == 0) {  // toggle every 2 seconds
    printTime(0, 0);
    lcd.setCursor(9, 0);
    float Ah = (float)cellmAs / 3600000000.0;   // show Ah
    lcd.print(Ah,3);
    lcd.print("Ah");
    
    lcd.setCursor(0, 1);

    if (cellVoltage < 100) lcd.print(" ");        // align cellvoltage
    if (cellVoltage < 1000) lcd.print(" ");
    if (cellVoltage < 10000) lcd.print(" "); 
    lcd.print(cellVoltage);                       //        5 char
    lcd.print("mV");                              //        2 char

    if (cellCurrent < 10) lcd.print(" ");         // align cellcurrent
    if (cellCurrent < 100) lcd.print(" ");
    if (cellCurrent < 1000) lcd.print(" ");
    lcd.print(cellCurrent);                       //        4 char
    lcd.print("mA ");                             //        3 char
    lcd.print(chargeStateString[actChargeState]); //        2 char
  } else {
    lcd.setCursor(0, 0);
    lcd.print("                ");
    lcd.setCursor(0, 0);
    lcd.print(cellTempSlope*10,3);
    lcd.setCursor(8, 0);
    lcd.print(cellTempFiltered,1);              // show Temperature
    lcd.print("C");
    lcd.setCursor(14,0);
    lcd.print(slopeDetectionCounter);

    lcd.setCursor(0, 1);
#ifdef measureRI
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(cellRI,3);
    lcd.print("Ohm");
#endif
  }
  lcd.noBlink();
}

//******************************************
// print menu
//******************************************
const char CellTypestring[4][5] = {"NiCd","NiMh","LiPo","Dchg"};
void printMenu (MenuState menuState) {
  lcd.setCursor(0, 0);
  if (actType == Discharge) {
    lcd.print("Typ, Strom, Volt");              // complete reprint 1st line of menu    
  } else {
    lcd.print("Typ, Strom, Zeit");              // complete reprint 1st line of menu
  }
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

  if (actType == Discharge) {
    lcd.setCursor(11, 1); 
    if (minCellVoltageDischarge < 100) lcd.print(" ");       // align max discharge voltage
    if (minCellVoltageDischarge < 1000) lcd.print(" ");
    if (minCellVoltageDischarge < 10000) lcd.print(" ");
    lcd.print(minCellVoltageDischarge);         // print min discharge voltage
  } else {
    lcd.setCursor(12, 1); 
    if (maxRuntime < 100) lcd.print(" ");       // align max runtime
    if (maxRuntime < 1000) lcd.print(" ");
    lcd.print(maxRuntime);                      // print max runtime
  }

  // set blinking cursor at the end of the value
  int cursorpos = 0;
  switch (menuState) {
    case Type:
      cursorpos = 3;
      break;
    case Current:
      cursorpos = 9;
      break;
    case Time:
      cursorpos = 15;
      break;
    case Voltage:
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
#define SlopeMeasureDistance 240
#define SlopeMeasureIntervall 6
#define numberOfLastTemps (SlopeMeasureDistance / SlopeMeasureIntervall)
static float lastTemps[numberOfLastTemps];                    // array to store the old temperatures for slope calculation
static unsigned long lastTimes[numberOfLastTemps];            // array to store the old measure timestimes for slope calculation

#define CellRIMeasureIntervall 60
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
  int sensorValueT = ADC_2.getADCVal();
  int sensorValueI_discharge = ADC_3.getADCVal();
  
  if (sensorValueI_discharge<sensorValueI)
  {
    dischargeSwitch = false;
    cellCurrent = sensorValueI*mAInPerInc;  // Chargecurrent increments to mA
  }                  
  else
  {
    dischargeSwitch = true;
    cellCurrent = sensorValueI_discharge*mAInPerInc; // Dischargecurrent 
  }
  cellVoltage = int(sensorValueU*mVInPerInc-sensorValueI*mAInPerInc); // increments to mV
  cellmAs += cellCurrent * elapsedTime;                   // calculate mA mseconds

#ifdef logSensor
  if (sensorValueT >= 1) {
    cellTemperature = CPerInc*log(sensorValueT)+COffset;    // calculate temperature from ADC value
  } else  {
    cellTemperature = -42.0;
  }
#else
  cellTemperature = sensorValueT*CPerInc + COffset;       // calculate temperature from ADC value
#endif
  cellTempFiltered = cellTempFiltered*(1-tempFilter)+cellTemperature*tempFilter;  // calculate new filtered value


  unsigned long seconds = actMeasureTime / 1000;              // calculate actual seconds 
  if ((seconds % SlopeMeasureIntervall) == 0) {               // store a temperature measurement every minute
    
    // store filtered temp and timestape for slope calculation
    auto slopeIndex = (seconds/SlopeMeasureIntervall) % numberOfLastTemps;
    lastTemps[slopeIndex] = cellTempFiltered;                 
    lastTimes[slopeIndex] = lastMeasureTime;

    // calculate slope index of oldest stored temp
    int lastSlopeIndex = slopeIndex + 1;
    if (lastSlopeIndex >= numberOfLastTemps) { lastSlopeIndex = 0;}

    // calculate slope by differantion
    cellTempSlope = (cellTempFiltered - lastTemps[lastSlopeIndex])/(actMeasureTime-lastTimes[lastSlopeIndex])*1000;
  }

#ifdef measureRI
static int temprefoutvalue;
static int tempcellVoltage;
static int tempcellCurrent;
  if (measureCellRI) {
    measureCellRI = false;
    refoutvalue = temprefoutvalue;
    cellRI = (tempcellVoltage - cellVoltage) / (tempcellCurrent - cellCurrent); 
  }
  if ((seconds % CellRIMeasureIntervall) == 0) {
    measureCellRI = true;  
    temprefoutvalue = refoutvalue;
    refoutvalue = 0;
    tempcellVoltage = cellVoltage;
    tempcellCurrent = cellCurrent;
  }
#endif
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
  actChargeState=CHECK;
  delayCounter = 0;  
  runtimeMinutes = 0;
  cellmAs = 0;
  maxCellTempSlope = 0.0;
  auto deltaTemp = cellTempFiltered-cellTemperature;  // calculate difference of filtered temperature
  if ((deltaTemp > 0.5) || (deltaTemp < -0.5)) {      // do initialize if not near the unfiltered temperature
    cellTempFiltered = cellTemperature;
  }
}

void closedLoopCurrent() {
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
    actChargeState = WAIT;                                  // switch to WAIT state
  }
}

//******************************************
// calculate charge current 
//  each type has an individual calculation
//******************************************
void calcChargeCurrent() {
static int voltageDetectionCounter=0;  
static float startTemperature = 0.0;
  switch (actType) {
    case NiCd:
      switch (actChargeState) {
        case CHECK:
        case Cc:
        case CC:
          actChargeState = CC;
          refoutvalue = chargeCurrent/mAOutPerInc;            // constant charge current during complete time
          break;
        case FULL:
          refoutvalue = 0;                                  // switch of current
          message = "NiCd FULL       ";                     // set message for display
          actChargeState = WAIT;                            // next charge state is Waiting
          break;
        case WAIT:
        default:
          break;
      }
      break;
    case NiMh:
      switch (actChargeState) {
		    case CHECK:
          voltageDetectionCounter++;
          if (voltageDetectionCounter>(fractionOfSecond*2)) { // wait 2 seconds to measure stable values
            voltageDetectionCounter = 0;                      // reset delay counter for next usage

            // initialize lastTemp and lastTime array for slope calculation 
            unsigned long seconds = millis() / 1000;          // calculate actual seconds 
            for (int i = (numberOfLastTemps-1); i==0; i--) {
              seconds -= SlopeMeasureIntervall;               // calculate timestamps in the past
              lastTemps[i] = cellTempFiltered;                // constant cellTemperature to calculate 0 slope at the beginning
              lastTimes[i] = seconds;                         // store timestamp
            }
            startTemperature = cellTempFiltered;              // set start temperature at beginning of charge
            actChargeState = Cc;                              // next charge state is init for constant current
          }
		      break;
        case Cc:
          refoutvalue = chargeCurrent/mAOutPerInc;            // constant current als long as voltage is lower than the limit   
		      actChargeState = CC;                                // next state is charging at constant current
          break;
        case CC:
          closedLoopCurrent();                                // closed loop control for charge current
          if (cellTempFiltered > 45.0) {                      // allow maximum temp of 45°C before end of charge 
              refoutvalue = 0;                                // switch of current
              message = "NiMh overtemp   ";                   // set message for display   
			        actChargeState = WAIT;
          }
          if (cellTempFiltered > startTemperature+3.0) {      // start temp slope detection above 25°C
            // store max slope
            if (cellTempSlope > maxCellTempSlope) { maxCellTempSlope = cellTempSlope;}
            if (cellTempSlope < maxCellTempSlope) {           // if slope is falling again we reached the end of charge
              slopeDetectionCounter++;                        // increment slopeDetectionCounter until 240s is reached
              if (slopeDetectionCounter > (fractionOfSecond*4*60)) {
                actChargeState = FULL;                        // next charge state is FULL
              } 
            } else {
              slopeDetectionCounter = 0;                      // new max slope found, restart timeout 
            }
          } else {
              slopeDetectionCounter = -1;                     // indicate that slope detection isn't started at all
          }
          break;
        case FULL:
            refoutvalue = 0;                                  // switch of current
            message = "NiMh FULL       ";                     // set message for display
            actChargeState = WAIT;                            // next charge state is Waiting
          break;  
        case WAIT:
          break;
        default:
          refoutvalue = 0;                                    // switch of current
          break;
      }
      break;
    case LiPo:
      switch (actChargeState) {
        case CHECK:
          refoutvalue = (chargeCurrent/mAOutPerInc)/10;       // min charge current is 10% of rated charge current
          delayCounter++;
          if (delayCounter >= fractionOfSecond*10) {          // delay for 10 seconds
            delayCounter = 0;   
            
            if (cellVoltage < maxConstCurrentVoltageLiPo) {   // cell is empty -> switch to constant current
              actChargeState = Cc;
            } else if (cellVoltage < maxCellVoltageLiPo) {    // cell nearly full -> switch to constant voltage
              actChargeState = CV;
            } else {                                          // cell full -> switch end of charge
              actChargeState = FULL;
            }         
          }
          break;

        case Cc:    // initialice Constant Current
          refoutvalue = chargeCurrent/mAOutPerInc;            // constant current als long as voltage is lower than the limit   
          actChargeState = CC;     
          break;
        case CC:
          closedLoopCurrent();          
          if (cellVoltage > maxConstCurrentVoltageLiPo) {         // detect state change because cellVoltage above constant current limit
            voltageDetectionCounter++;                            // delay state change 10s 
            if (voltageDetectionCounter > (1*fractionOfSecond)) {
              actChargeState = CV;                                // next state constant voltage
              voltageDetectionCounter = 0;                        // reset delay counter for next usage
            }
          }else {                                                 // reset state change because voltage to low again
            voltageDetectionCounter = 0;
          }          
          break;
        case CV:
          // regulate cell voltage by adjusting the current
          if (cellVoltage > (maxConstCurrentVoltageLiPo + 20)) {
            refoutvalue--;                                        // reduce charge current
            if (refoutvalue < 0) refoutvalue=0;                   // prevent underrun of current
          }
          if (cellVoltage < (maxConstCurrentVoltageLiPo - 20)) {
            refoutvalue++;                                        // increase charge current
            if (refoutvalue > chargeCurrent/mAOutPerInc) {        // prevent higher currents than allowed
              refoutvalue = chargeCurrent/mAOutPerInc;
            }
          }
          if (refoutvalue < ((chargeCurrent/mAOutPerInc)/10)) {   // stop charging at 10% of initial charge current
            actChargeState = FULL;
          }
          if (cellVoltage > maxCellVoltageLiPo) {                 // detect end of charge bewcause cellVoltage above max cell voltage
            voltageDetectionCounter++;                            // delay state change 10s 
            if (voltageDetectionCounter > (10*fractionOfSecond)) {
              actChargeState = FULL;                              // next state FULL
              voltageDetectionCounter = 0;                        // reset delay counter for next usage          
            }
          }else {                                                 // reset state change because voltage to low again
            voltageDetectionCounter = 0;          
          }
          break;
        case FULL:
          refoutvalue = 0;                                        // switch of current
          message = "LiPo FULL       ";                           // set message for display
          actChargeState = WAIT;                                  // next state WAIT
          break;
        case WAIT:
          break;
      }
      break;
    case Discharge:
      switch (actChargeState) {
        case CHECK:
          refoutvalue = (chargeCurrent/mAOutPerInc)/10;       // min charge current is 10% of rated charge current
          
          delayCounter++;
          if (delayCounter >= fractionOfSecond*2) {           // delay for 2 seconds
            delayCounter = 0;   
            
            if (dischargeSwitch == true) {
              actChargeState = Cc;
            } else {
              // check timeout of external switch and report error message
              message = "wrong switch pos";
              refoutvalue = 0;
              actChargeState = WAIT;
            }
          }
          break;
        case Cc:
          refoutvalue = chargeCurrent/mAOutPerInc;            // constant current als long as voltage is lower than the limit   
          actChargeState = CC;                                // next state is charging at constant current
          break;
        case CC:
          closedLoopCurrent();                                // closed loop control for charge current
          if (cellVoltage < minCellVoltageDischarge) {
            actChargeState = FULL;
          }
          break;
        case FULL:
          refoutvalue = 0;
          message = "Cell empty      ";
          actChargeState = WAIT;
          break;
        case WAIT:
          break;
      }
      break;
    default:
      refoutvalue = 0;                                            // switch of current
      break;
  }
  if (runtimeMinutes >= maxRuntime) 
  {
    refoutvalue = 0;
    message = "Timeout         ";                                 // set message for display

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
  lcd.print("                ");  
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
void monitorValues() {
static int enableBits=0xff;

  // if we get a valid byte, read analog ins:
  if (Serial.available() >= 3) {
    enableBits = Serial.parseInt();
//    Serial.print("got message");
//    Serial.print(enableBits);
  }
  if (enableBits & 0x01) Serial.print(cellVoltage);
  Serial.print(" "); 
  if (enableBits & 0x02) Serial.print(refoutvalue*mAOutPerInc);
  Serial.print(" "); 
  if (enableBits & 0x04) Serial.print(cellCurrent);
  Serial.print(" "); 
  if (enableBits & 0x08) Serial.print(cellTempFiltered);
  Serial.print(" "); 
  if (enableBits & 0x10) Serial.print(cellTempSlope,5);
  Serial.print(" "); 
  if (enableBits & 0x20)Serial.print(actChargeState);
  Serial.println();
}

//******************************************
// main loop
//******************************************
void loop() {
static int count = 0;
static int delayMenu = 5*fractionOfSecond;            // delay 5s to show splash
  ADC_0.serviceADCPin();
  ADC_1.serviceADCPin();
  ADC_2.serviceADCPin();
  ADC_3.serviceADCPin();
  count++;
  if ((count%10)==0) {
    getChargeState();                 // read analog values and calculate cell values
    calcChargeCurrent();              // calculate charge current and state
    setChargeCurrent();               // output charge current
    monitorValues();
  
    processButtons();                 // read and process buttons
  
    if (cellCurrent <= 5) {           // check charging depending on current flow to detect a cell
      if (!measureCellRI) charging = false;
    } else {
      if (message == 0) {
        if (charging == false) {        // check for start of charging positive edge
          initCharging();               // init charging state
          clearRunTime();               // Zeit zurücksetzen
          charging = true;              
        }
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
