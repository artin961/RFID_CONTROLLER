#include "Wiegand.h"
#include <EEPROM.h>



#define INTERNALEEPROM

#ifndef INTERNALEEPROM
#include <Wire.h>
#include "SparkFun_External_EEPROM.h" // Click here to get the library: http://librarymanager/All#SparkFun_External_EEPROM
#endif




//#define EEPROM_SIZE 512     //24C04
//#define EEPROM_SIZE 1024    //24C08
//#define EEPROM_SIZE 2048    //24C16
//#define EEPROM_SIZE 4096    //24C32
//#define EEPROM_SIZE 8192    //24C64
//#define EEPROM_SIZE 16384   //24C128
//#define EEPROM_SIZE 32768   //24C256
//#define EEPROM_SIZE 65536   //24C512






/*  IO
  PA0 RELAY
  PA1 DOOR_DETECT RID
  PA2 RESET_BUTTON
  PA3 LED
  PA4 DATA
  PA5 BUZZER
  PA6 SDA
  PA7 OPEN_BUTTON
  PB0 DATA0
  PB1 DATA1
  PB2 INTERRUPT FOR DATA 0 AND 1
*/

#define wdt_reset() __asm__ __volatile__ ("wdr")

#define OPEN_TIME 5000
#define btnDEB 50

#define CKSUM 128
#define CKSUMADDR 9

#define CODE_EMPTY  0xFFFFFFFFFFFFFFFFULL
#define CARDSSTARTADDR 10

#ifdef INTERNALEEPROM
#define CARDSNUM 120
#endif

#define MASTERTIME 30 ///in seconds
#define MASTERLEARNTIME 30 //IN SECONDS

///TIMES TO HOLD RESET BUTTON TO ENTER MODES
#define MASTERLEARNENTERTIME 10000// 10 seconds hold reset button to enter master learning
#define FULLRESETTIME 30000 //30 seconds hold reset button to erase all


////TONES
#define CHIRP_1KHZ 500  ///TO GENERATE 1KHZ TONE USING CHIRP FUNCTION
#define CHIRP_2KHZ 250  ///TO GENERATE 2KHZ TONE USING CHIRP FUNCTION
#define CHIRP_4KHZ 125  ///TO GENERATE 4KHZ TONE USING CHIRP FUNCTION

WIEGAND wg;

#ifndef INTERNALEEPROM
#define CARDSNUM 240
ExternalEEPROM EE; //EXTERNAL EEPROM
#endif



////GENERAL+
uint8_t ledCounter = 0;
unsigned long doormillis = 0; ///REMAINING MILLISECONDS TO HOLD DTHE DOOR OPEN
unsigned long millisEverySec = 0; ///FOR EVERY SECOND/10 COUNTER
uint8_t tentofsec = 0; //FOR EVERY SECONDS

///MANUAL OPEN BUTTON
bool btnREL = true;//VARIABLE TO PREVENT BUTTON FROM OPENING THE DOOR IF STUCK SHORTED
unsigned long btnmillis = 0;  ///TO DEBOOUNCE THE BUTTON FOR MANUAL OPEN

///MASTER MODE
unsigned long MASTERCODE = CODE_EMPTY;    ////OUR MASTER CODE/// DEFAULT 0
uint8_t masterLearnSecs = 0; //MASTER LEARNING DISABLED
uint8_t masterSecs = 0; //FOR REMAINING TIME IN MASTER seconds  if we are in master mode they are more than 0



///PROGRAMMING BUTTON
unsigned long pbtnmillis = 0; ///TO DEBOUNCE PROGRAMMING BUTTON HELD DOWN
unsigned long pbtnmillisr = 0; ///TO DEBOUNCE PROGRAMMING BUTTON RELEASED
bool prgbut = false; // IF PROGRAMMING BUTTON WAS HELD DOWN



void setup() {
  DDRA = (1 << PA0) | (1 << PA3) | (1 << PA5); ///DIGITAL OUTPUTS PORT A RELAY LED BUZZER
  PORTA = (1 << PA3) | (1 << PA1) | (1 << PA2) | (1 << PA7); //LED HIGH ON BOOT  AS WELL AS PULL UPS ON RID RESET AND MANUAL OPEN

  PORTB = (1 << PB0) | (1 << PB1) | (1 << PB2);  //PULLUPS ON DATA0 DATA1 AND INT0
  ///SETUP WATCHDOG TIMER
  WDTCSR |= (1 << WDP0) | (1 << WDP3) | (1 << WDE);
  wdt_reset();  // Reset the watchdog timer

#ifndef INTERNALEEPROM

  Wire.begin();
  Wire.setClock(400000);

  if (!EE.begin(0x50, Wire))
  {
    showError(3);
  }
  else
  {
    EE.setMemorySize(EEPROM_SIZE);
    chirp(50, CHIRP_2KHZ);
    chirp(50, CHIRP_1KHZ);
    chirp(50, CHIRP_4KHZ);
    chirp(50, CHIRP_1KHZ);
  }
#endif

  if (validateEEPROM())//IF WE HAVE VALID INFO INTO EEPROM
  {
    loadEEPROM();
    chirpEXITPRG; //EXITPRG SOUND TO INDICATE RESET
  }
  else
  {
    if (initEEPROM()) //ELSE INIT EEPROM
      chirpRESET();   //SUCSESSFULL INIT
    else
      showError(2);
  }



  wg.begin();

  PORTA &= ~(1 << PA3); //LED OFF TO INDICATE NORMAL BOOT
}

void loop() {
  if (wg.available())//IF WE HAVE CODE FOR PROCESSING
  {
    wdt_reset();
    //IF WE DONT HAVE MASTER CODE
    if (MASTERCODE == CODE_EMPTY && masterLearnSecs > 0) //IF WE DONT HAME MASTER CODE MAKE THIS ONE A MASTER
    {
      masterLearnSecs = 0; ///DISABLE MASTER LEARNING FOR THE FUTURE
      //TODO WRITE WRITE MASTER CODE INTO FIRST SECTOR AND READ IT BACK TO VERIFY, IF OK MAKE ENTER EXIT TONE TO INDICATE OR ELSE
#ifdef INTERNALEEPROM
      EEPROM.put(0, wg.getCode());//WRITE MASTER CODE TO EEPROM
      EEPROM.get(0, MASTERCODE);
#else
      EE.put(0, wg.getCode());//WRITE MASTER CODE TO EEPROM
      EE.get(0, MASTERCODE);
#endif
      if (MASTERCODE == wg.getCode())
      {
        chirpENTRPRG();//ENTER EXIT TONES TO INDICATE MASTER LEARNED
        chirpEXITPRG();//
      }
      else
        chirpERR();//MASTER NOT LEARNED MAYBE WRITE PROTECTED EEPROM
    }
    else
    {
      if (wg.getCode() == MASTERCODE && MASTERCODE != CODE_EMPTY) //IF WE GOT THE MASTER CARD AND MASTER CARD IS NOT 0 SO MASTERING FROM CARD IS ENABLED
        toggleMASTER();
      else ///IT IS NOT HE MASTER CODE CHEK IN DB
      {
        if (masterSecs > 0) ///IF WE ARE IN PROGRAMMING MODE
        {
          masterSecs = MASTERTIME; ////RESET TIMER AND GIVE MORE TIME FOR WRITING KEYS
          if (checkDB(wg.getCode()) == -1) { //IF WE DONT HAVE IT IN DB
            addDB(wg.getCode());////WE ADD IT
            if (checkDB(wg.getCode()) != -1)
              chirpOK();//CARD LEARNED OK
            else
              chirpERR();//CARD NOT LEARNED MAYBE WRITE PROTTECTED
          }
          else { ///WE HAVE IT IN DB
            remDB(wg.getCode());//SO WE REMOVE IT
            if (checkDB(wg.getCode()) == -1)
            { ///CARD ERASED OK
              chirpOK();
              delay(100);
              chirpOK();
              delay(100);
              chirpOK();
            }
            else///ERASE ERROR MAYBE WRITE PROTTECTED
              chirpERR();
          }
        }
        else  ///WE ARE NOT IN MASTER MODE WE CHECK IF IN DB TO OPEN DOOR
        {
          if (checkDB(wg.getCode()) != -1) ///CHECK IF WE HAVE CODE IN DB
            openDoor();///open door
          else
            chirpERR();///error unauthorised
        }
      }///end of db checking
    }
  }///end of available code


  if (!(PINA & (1 << PA7)))///IF MANUAL OPEN BUTTON IS HELD DOWN
  {
    if (millis() - btnmillis > btnDEB && !(PORTA & (1 << PA0))  && btnREL)
    {
      openDoor();
      btnREL = false;//VARIABLE TO PREVENT BUTTON FROM OPENING THE DOOR IF STUCK SHORTED
    }
  }
  else
  {
    btnmillis = millis();//DEBOUNCING BUTTON
    btnREL = true;//VARIABLE TO PREVENT BUTTON FROM OPENING THE DOOR IF STUCK SHORTED
  }

  wdt_reset();
  if (!(PINA & (1 << PA2)))///IF RESET BUTTON IS HELD DOWN
  {
    unsigned long tmp = millis() - pbtnmillis;
    if (tmp > btnDEB && !prgbut)
    {
      prgbut = true;
      pbtnmillisr = millis();
    }
    if (tmp > MASTERLEARNENTERTIME && tmp < FULLRESETTIME)
    {
      PORTA |= (1 << PA3); //GREEN ON DOOR IS HELD OPEN
    }
    else if (tmp > FULLRESETTIME)
    {
      PORTA &= ~(1 << PA3); //GREEN OFF DOOR IS HELD OPEN
    }
  }
  else
  {
    pbtnmillis = millis();
    if (millis() - pbtnmillisr > btnDEB && prgbut)
    {
      prgbut = false;
      if (millis() - pbtnmillisr > FULLRESETTIME) /////IF BUTTON HELD MORE THAN 30 SECONDS ERASE ALL
      {
        if (initEEPROM()) //IF MASTER CODE IS ACTUALLY 0
          chirpOK();
        else //MAYBE EEPROM WRITE PROTECTED
          showError(2);
      }
      else if (millis() - pbtnmillisr > MASTERLEARNENTERTIME)  ///BUTTON HELD MORE THAN 10 SECS BUTE LES THAN 30 ERASE MASTER ONLY
      {
        if (eraseMaster()) //IF MASTER CODE IS ACTUALLY 0
          chirpOK();
        else //MAYBE EEPROM WRITE PROTECTED
          chirpERR();
      }
      else if (millis() - pbtnmillisr > 3000) ///IF BUTTON HELD MORE THAN 3 SECONDS BYT LESS THAN 10 ENTER EXIT PRG MODE
      {
        toggleMASTER();
      }
      else if (millis() - pbtnmillisr > 500)/// IF BUTTON HELD FOR MORE THAN HALF A SEC BUT LES THAN 3 OPEN DOOR
      {
        if (masterSecs > 0)
          toggleMASTER();
        else if (masterLearnSecs > 0)
        {
          masterLearnSecs = 0;
          chirpEXITPRG();
        }
        else
          openDoor();
      }
    }
  }


  if (PORTA & (1 << PA0) && millis() - doormillis > OPEN_TIME) //if door is open and need to close it
  {
    chirpEXITPRG();//LOCKING SOUND
    cli();//stop interrupts
    delay(10);
    PORTA &= ~(1 << PA0); //LOCK THE DOOR
    delay(100);
    sei();//enable interrupts
  }

  wdt_reset();
  if (millis() - millisEverySec > 100) {///EVERY 100 miliseconds task
    millisEverySec = millis();
    tentofsec++;


    if ((PINA & (1 << PA2)))
    {
      if ((masterSecs > 0) || (MASTERCODE == CODE_EMPTY && masterLearnSecs > 0)) //IF IN PROIGRAMING OR MASTER LEARNING MODE BLINK THE LED
      {
        if (ledCounter > 0)
          ledCounter--;
        else
        {
          PORTA ^= (1 << PA3); //GREEN READYLED ON
          ledCounter = (masterSecs > 0) ? 5u : 0u;///IF IN PROG MODE BLINK SLOW ELSE BLINK FAST
        }
      }
      else
      {
        if (PORTA & (1 << PA0))
          PORTA |= (1 << PA3); //GREEN ON DOOR IS HELD OPEN
        else
          PORTA &= !(1 << PA3); //GREEN OFF DOOR IS LOCKED
      }
    }

  }
  wdt_reset();

  if (tentofsec > 10) {///EVERY SECOND TASK
    tentofsec = 0;
    if (masterSecs > 0)////FOR MASTER MODE EXITING
    {
      masterSecs--;
      if (masterSecs == 0) /// that we ended master mode
        chirpEXITPRG();
    }
    if (masterLearnSecs > 0)////FOR MASTER learning mode EXITING
    {
      masterLearnSecs--;
    }

  }

}
///CHECKS IF CARD CODE IS IN THE DB/ returns cardnum if found, else -1
int checkDB(unsigned long code) {
  int foundat = -1;
  for (uint16_t i = 0; i < CARDSNUM; i++)
  {
    unsigned long tmpcode = CODE_EMPTY;
    uint16_t addr = CARDSSTARTADDR + (i * sizeof(code));
#ifdef INTERNALEEPROM
    EEPROM.get(addr, tmpcode);
#else
    EE.get(addr, tmpcode);
#endif
    if (tmpcode == code)
    {
      foundat = i;
      break;
    }
  }
  return foundat;
}
int addDB(unsigned long code) {
  int addedat = -1;
  for (uint16_t i = 0; i < CARDSNUM; i++)
  {
    uint16_t addr = CARDSSTARTADDR + (i * sizeof(code));
    unsigned long tmpcode = CODE_EMPTY;
#ifdef INTERNALEEPROM
    EEPROM.get(addr, tmpcode);
#else
    EE.put(addr, code);
#endif

    if (tmpcode == CODE_EMPTY)//IF WE FIND A HOLE WE INSERT
    {
#ifdef INTERNALEEPROM
      EEPROM.put(addr, code);
#else
      EE.put(addr, code);
#endif
      addedat = i;
      break;
    }
  }
  return addedat;
}
void remDB(unsigned long code) {
  bool inDB = false;
  for  (uint16_t i = 0; i < CARDSNUM; i++)
  {
    uint16_t addr = CARDSSTARTADDR + (i * sizeof(code));
    unsigned long tmpcode = CODE_EMPTY;
#ifdef INTERNALEEPROM
    EEPROM.get(addr, tmpcode);
#else
    EE.get(addr, tmpcode);
#endif
    if (tmpcode == code)
    {
      unsigned long tmp = CODE_EMPTY;
#ifdef INTERNALEEPROM
      EEPROM.put(addr, tmp);
#else
      EE.put(addr, tmp);
#endif
      break;
    }
  }
}
uint8_t validateEEPROM() {
#ifdef INTERNALEEPROM
  return (EEPROM.read(CKSUMADDR) == CKSUM);
#else
  return (EE.read(CKSUMADDR) == CKSUM);
#endif
}
void toggleMASTER() {//TOGGLE MASTER LEARNING OR DELEATING MODE MODE FROM CARD AND BUTTON
  if (!masterSecs)
  {
    masterSecs = MASTERTIME; ///INDICATE WE ARE IN PROGRAMING MODE AND HAVE MORE TIME
    chirpENTRPRG();
  }
  else
  {
    masterSecs = 0; //EXIT MASTER MODE
    chirpEXITPRG();
  }
}
uint8_t initEEPROM() {
  unsigned long tmp = CODE_EMPTY;
  for (uint16_t i = 0; i < CARDSNUM; i++)
  {
    uint16_t addr = CARDSSTARTADDR + (i * sizeof(tmp));
#ifdef INTERNALEEPROM
    EEPROM.put(addr, tmp);
#else
    EE.put(addr, tmp);
#endif
  }
#ifdef INTERNALEEPROM
  EEPROM.put(0, tmp);
  EEPROM.write(CKSUMADDR, CKSUM);
#else
  EE.put(0, tmp);
  EE.write(CKSUMADDR, CKSUM);
#endif
  return validateEEPROM();
}
uint8_t eraseMaster() {
  unsigned long tmp = CODE_EMPTY;
#ifdef INTERNALEEPROM
  EEPROM.put(0, tmp);
  EEPROM.get(0, MASTERCODE);
#else
  EE.put(0, tmp);
  EE.get(0, MASTERCODE);
#endif
  if (MASTERCODE == CODE_EMPTY) //MASTER ERASED SUCCESSFULLY
  {
    masterLearnSecs = MASTERLEARNTIME; //MASTER LEARN ENABLE
    return 1;//SUCCESS
  }
  else
  {
    masterLearnSecs = 0;//DISABLE MASTER LEARNING THERE WAS ERROR
    return 0;//ERROR
  }
}
void loadEEPROM() {
#ifdef INTERNALEEPROM
  EEPROM.get(0, MASTERCODE);//LOAD MASTER CODE INTO RAM
#else
  EE.get(0, MASTERCODE);
#endif
}
void openDoor() {
  if (!PORTA & (1 << PA0))///IF DOOR IS NOT HELD OPEN ALREADY
  {
    doormillis = millis();//RESET DOOR COUNTER MILLISECONDS
    PORTA |= (1 << PA0); //LED HIGH AND RELAY OPEN
    chirpOK();
  }
}
void chirpENTRPRG() {
  chirp(200, CHIRP_2KHZ);
  chirp(200, CHIRP_4KHZ);
}
void chirpEXITPRG() {
  chirp(200, CHIRP_4KHZ);
  chirp(200, CHIRP_2KHZ);
}
void chirpOK() {
  chirp(50, CHIRP_2KHZ);
  delay(50);
  chirp(50, CHIRP_2KHZ);
}
void chirpLEARNOK() {
  for (uint8_t i = 0; i < 2; i++)
  {
    chirpOK();
    delay(50);
  }
}
void chirpERASEOK() {
  for (uint8_t i = 0; i < 3; i++)
  {
    chirpOK();
    delay(50);
  }
}
void chirpERR() {
  chirp(200, CHIRP_2KHZ);
  delay(100);
  chirp(200, CHIRP_2KHZ);
  delay(100);
  chirp(200, CHIRP_2KHZ);
}
void chirpRESET() {
  for (uint8_t i = 0; i < 10; i++)
  {
    chirp(50, CHIRP_4KHZ);
    delay(50);
  }
}
void chirp(int playTime, int delayTime) {///FUNCTION TO MAKE BUZZES
  long loopTime = (playTime * 1000L) / (delayTime);
  for (int i = 0; i < loopTime; i++) {
    PORTA ^= (1 << PA5); //TOGGLE BUZZER
    delayMicroseconds(delayTime);
  }
  PORTA &= ~(1 << PA5);//BUZZER PIN OFF JUST IN CASE
}
void showError(uint8_t err) {
  while (1)
  {
    wdt_reset();
    for (uint8_t i = 0; i < err; i++)
    {
      chirp(300, CHIRP_2KHZ);
      delay(300);
    }
    delay(5000);
  }
}
