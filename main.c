//#define DEBUG   // when DEBUG is enabled, then sit down with pen and paper, and write down all the LED flash-codes, it helps a lot.
                // es ist glueckssache ob der POR erkannt wird, das ist unbrauchbar fuer den benutzer.  Wenn der Benutzer 
                // den Strom unterbricht, muss er die sicherheit haben, dass es kein BOD, sondern wirklich ein POR.
                // weil es sonst nicht moeglich ist eine andere geschwindigkeit im EEPROM zu speichern.  deshalb wird 
                // bei abgeschalteten DEBUG IMMER die tasten abgefragt.

//
// PIC12F675  GP0->GND start joystick autofire, GP1->GND stop autofire, GP5=wired-OR fire-signal, GP4=LED
//  by Oliver Kaltstein - 2020 Aug 11
#define DATE    // I want to include date and time in EEPROM and everywhere, so if I forget to set new version # I always know what's the newest
#define VERSION 1
#define AUTHORS // Chris! Wo zum Teufel kann ich solche Daten zentral definieren, so dass sie immer ueberall richtig angezeigt werden?


//
//  How to compile:   xc8 --RUNTIME=+resetbits --chip=12F675 main.c
//


//  How to Use
//
// plug joystick into Commodore/Atari/Amiga/VCS computer-system.  there should be three flashes visible, indicating that 
// the system has been initialized with speed "3".  Press START to start autofire.  Press STOP to stop autofire.
// Joysticks with newer PICs that have EEPROM functionality will remember the speed.  Joysticks with older picks will 
// forget after unplugging for one second.  To reprogram the fire-speed, unplug the joystick, hold the START key to 
// increase the speed, or hold the STOP key to decrease the speed.  The LED flashes should show the new, programmed speed.


// TODO:  debug init routine - POST? WDT? BDT?  are they really always reporting the correct resetlevel ?
//  Implemented ICSP for firmware updates,  done.

// why no interrupts?  interrupts cause the stack to grow, external key interrupts are not recommended by microchip 
// this cheap joystick autofire system supports only polling, which happens during every delay-loop.
//
//     https://www.microchip.com/forums/m749119.aspx   
//
//     Its quite tricky to do that in a non-blocking ISR activated by an interrupt from the button.   
//     Its a really really bad idea to use delays in an ISR because the main program and any other interrupts 
//     at the same priority level are blocked until the ISR returns.   Also, due to contact bounce, you will 
//     get a large number of interrupts in a short time when you press the button once and it is quite 
//     likely to also bounce on release, causing more interrupts. 
//

// BEGIN CONFIG
//# pragma config FOSC = HS     	// external XTAL 
# pragma config FOSC = INTRCIO 		// internal RC oscillator 
# pragma config WDTE = ON 		// watchdog timer
//# pragma config PWRTE = OFF 		// power-up timer
#ifdef DEBUG
   # pragma config BOREN = ON 		// trying to DEBUG BrownOutDetect (BOD), but problems interpreting the bits.
#else
   # pragma config BOREN = OFF 		// no brown-out because no battery used. I think watchdog and reset should fix power-failures.
#endif
//#pragma config CPD = ON		// data EEPROM code protection 
//#pragma config CP = ON 		// flash program code protection
#ifdef DEBUG
   #pragma config MCLRE = ON 		// don't have external reset logic, but keep a 10k ohm external pull-up resistor ! 
                                        // even a 10k pull-down would work to avoid floating, but it's bad style in case 
                                        // /MCLRE is external, then a pull-down would cause a constant reset, so don't do it!
                                        // because experiments showed that the reset-input is VERY-SENSITIVE causing 
                                        // crashed even when disabled !!!   Is this a Microchip Haredware BUG ?
#else
   #pragma config MCLRE = OFF 
#endif
//or use this compact format instead: #pragma config FOSC=INTRCIO,WDTE=OFF,MCLRE=OFF,BOREN=OFF
//END CONFIG


#define _XTAL_FREQ 4000000    		// PIC internal RC based system-clock 
                                        // TODO: tune speed, check ASM if calibration is written into register.  
                                        // check frequencies of effective output pulses.  
                                        // understand what kind of internal clock freq actually work.
//#include <stdio.h>
//#include <stdlib.h>
#include <xc.h>


// Theory Of Operation:
//
// multiple joystick-buttons are pulling the FIRE-line towards LOW, so 
// the joystick-fire-line may never be driven to HIGH, this would cause 
// a short-circuit while another button is pressed by a player.
// therefore valid logic levels are only:
//
// LOW - button or autofire is being pressed
// TRI - button or autofire is being unpressed, pullup provided by game-computer.
//
// further note: PIC's built in  PCM output cannot be used, because PCM toggles between LOW and HIGH, 
//               while LOW and TRI states are actually required!


// Port Assignments

//                                +----\/----+
//                        5V  Vdd | 1      8 |  Vss  GND
//   FIRE   T1CKI/OSC1/CLKIN  GP5 | 2      7 |  GP0  AN0/CIN+/ICSPDAT     START
//   LED AN3/T1G/OSC2/CLKOUT  GP4 | 3      6 |  GP1  AN1/CIN-/Vref/ICSPCLK STOP
//   IMPORTANT NOTE *  /MCLR  GP3 | 4      5 |  GP2  ANS2/TOCKI/INT/COUT
//                                +----------+
//                                   12F675
// * IMPORTANT NOTE
// don't leave /MCLR floating, it will ruin the stability of the PIC12F508 or PIC12F675.  The datasheet is incorrect!
// https://ww1.microchip.com/downloads/en/devicedoc/41190c.pdf

                // Vdd pin1  5V "drain"  -make sure to unplug joystick from computer when reprogramming    //ICSP 5v
#define FIRE  5 // gp5 pin2  output-FIRE (tristate or low) 0V will fire beause C64 joyport is active low
#define LED   4 // gp4 pin3  output-LED  0V will turn LED on 
                // gp3 pin4  is input-only, for reset or other inputs.                                     //ICSP prg volt
                // gp2 pin5  (was not suitable for input on 12c508),  seems to be the only interrupt input
#define STOP  1 // gp1 pin6  STOP-autofire  internel pull-up-resistor  0V=logical 0  switch against ground // ICSP clock
#define START 0 // gp0 pin7  START-autofire internal pull-up-resistor  0V=logical 0  switch against ground // ICSP data
                // Vss pin8  GND "source"                                                                  // ICSP ground



// blink-codes - indicating shooting-styles, warnings, errors, or debug-locations in code.

// shooting codes cannot be calculated, therefore must be read from some array or record/structure.

/* enum MsgCode {  islamic, canon, handgun, aircraft, butcher, holocaust, satanic }; */

//  CALCULATIONS 
                    // (note:  the lamp cannot be flashed zero times, so 0 is an illegal value)
#define ISLAMIC   0 // TOOSLOW autofire 0.0002Hz ~ 200ms + 3600s(1 hour) - bomb/knife  kills 1 "unbeliever" per hour.  for Tests only!
#define CANON     1 // VERYSLOW autofire 0.5Hz ~ 200ms + 1800ms  - handgun or light machine gun 
#define HANDGUN   2 // SLOW autofire 2Hz ~ 200ms +300ms
#define AIRCRAFT  3 // NORMAL  autofire 7Hz ~ 71ms + 72ms
#define BUTCHER   4 // FAST autofire 15Hz ~ 33ms + 33ms
#define HOLOCAUST 5 // VERYFAST autofire 30Hz ~ 17ms + 17ms
#define SATANIC   6 // TOOFAST autofire 666Hz ~ really fast, but cannot be switched off, no key polling.  for Tests only !

// shoot[ ] is the 1st half of the pulse, reload[ ] is the 2nd half of the pulse.  shoot + reload = period in jiffies.  1 jiffy is 10ms
// I assume that pulses must be long enough to be polled by the game-computer, and may not be too long to trigger 
// multiple shots, if the game has a level-driven autofire already implemented.  TODO:  verify this assumption.

// keeping such data in the eeprom and keeping the eeprom space unprotected makes this 
// devcie "hackable", so autofire delays can be changed using merely a PIC programmer W/O having a compiler.

//  intended freq:               1Hz, 5Hz, 2Hz,7Hz,15Hz,30Hz,666Hz 
//eeprom const long int shoot[]  = {     10,  10,  10,  7,   3,   2,    0   };
//eeprom const long int reload[] = { 359990, 118,  40,  7,   3,   2,    0   };

//  intended freq:                     1h,0.5Hz, 2Hz,7Hz,15Hz,30Hz,666Hz
//  intended level:                           1,    2,  3,   4,   5  )
eeprom const long int shoot[]  = {     20,   20,  20,  7,   3,   2,   0     };
eeprom const long int reload[] = { 359980,  108,  30,  7,   3,   2,   0     };    // the first value cannot fit into "int".  Can PIC use long?

#define MIN CANON
#define MED AIRCRAFT
#define MAX HOLOCAUST


// fire speed reference
// TODO:  change later after seeing Quickshot output frequency on my oscilloscope in Germany 

/*   TODO use these Round Per Minute (RPM) values and show names or values  on OLED display or LCD   
RPM guide:
https://encyclopedia2.thefreedictionary.com/Rounds+per+minute
 Maximum and effective rates of fire of certain types of weapons
 	Rate of fire (number of shots per minute)
 					Maximum		Effective
Submachine gun..............		400–1,000	40–120   = 80 RPM  1.3 Hz
Automatic rifle ...............		400–900		40–65    = 52 RPM  0.8 Hz
Light machine gun.............		500–1,000	60–150   = 100 RPM  1.6 Hz
Heavy machine gun............		600–800		150–300  = 200 RPM  3.3 Hz
Antiaircraft machine gun.........	500–1,000	80–300   = 200 RPM  3.3 Hz
Automatic antiaircraft gun........	200–1,000	200–1,000 = 500 RPM 8.3 Hz
Howitzer, cannon, recoilless gun ....		—	2–6
Mortar ....................			—	5–25
*/

// warnings, errors, and debug codes  (to be output as count of LED flashes, if no better output device is available)

#define WNOSOUND    7  // WARNING NOSOUND disable buzzer key beeps
#define WSOUND 	    8  // WARNING SOUND enable buzzer key beeps

// these error-numbers are reported to the user by Msg or any other available output-device

#define ECORRUPT    9 // ERROR illegal RAM or EEPROM value detected, RAM or EEPROM data reinitialized by sanity-checking. 
                               // Note that this is normal during first start of a new device
#define EROM 	   10 // ERROR ROM checksum error, go and have your controller repaired or replaced 

// only during Code Debug, these numbers will be reported thru Msg, since simulator-stimuli and PicKit3 are not working in MacOS X IDE
#ifdef DEBUG
   #define DINT    12 // DEBUG bit 1=1 INT  NOT USED:  humidity, light, shock, midnight, detected, punish user with  high voltage
   #define DPD     13 // DEBUG bit 3=1 IPD  woke up from sleep by /MCLR reset (external), what went wrong, cold- or warm-boot?...
   #define DTO     14 // DEBUG bit 4=1 ITO WDT timeout program crashed for EMP or software reson, partial reset, try to keep system running *EXCEPT 12bit cores
   #define DBOD    15 // DEBUG bit 5=1 BOD  brown-out-happened, data cannot be trusted, validation and conservative reinitialization
   #define DPOR    16 // DEBUG bit 7=1 POR  this is a cold boot power on reset: all RAM data initialized, EEPROM data should be validated
   #define DSTKUNF 17 // DEBUG bit 8=1 STKUNF stack was empty, this is hopeless, firmwareupgrade required.
   #define DSTKFUL 18 // DEBUG bit 9=1 STKFUL stack was full, this is hopeless, firmwareupgrade required.
   #define DEBUG19 19 // DEBUG general purpose to check program-branches "Msg(DEBUG19)"
   #define DEBUG25 25 // DEBUG general purpose to check program-branches "Msg(DEBUG25)"
   #define DEBUG30 30 // DEBUG general purpose to check program-branches "Msg(DEBUG30)"  use sparingly, who has the time to count 30 pulses.
#endif

// Vars

eeprom struct ABOUT
 {
    char Version[];
    char VersionString[];
    char VersionDate[];
    char Intimidation[];
 }  myStruct = {  
		  { VERSION, 0x00, 0x00 },   // TODO:  put defined version here
                  {"EEPROM\0"}, 
                  {"2020/08/12\0"},          // TODO:  put automatic date here
//                  {__DATE__, __TIME__},          // TODO:  put automatic date here
                  {"Made in Japan 2020 by Oliver Kaltstein. Have Fun! firespeed,errcount,powercycles,last:\0"},
               };

//TODO variables don't appear in the same sequence in EEPROM memory as in this C-source code, so I included them above.  can it be fixed?
//eeprom char label1[]="speed:\0";
eeprom long int autofirespeed=MED;  
					   // this initial value will be programmed by the PIC programmer only, 
            		                   // can be changed by sofftware any time in the EEPROM
                                	   // by holding down the START-key or the STOP-key on the controller during power-up.
                                           // BUG:  TODO:  this vaulue get's destroyed ->FFh in the PIC and therefore also read back into the 
                                           // programmer.  when reading the EEPROM with the K150 programmer! 
                                           // I think the reason is the capacity of the added cable and reset-switch on the Vpp line, 
                                           // a test showed, that PicKIT3 is stronger and could handle thie extra capacity.  
                                           // Slow Vpp due to manual resets are a known problem!!!
                                           // but apparently only the changing EEPROM values gut damaged, constant EEPROM and PROG values 
                                           // didn't get damaged!  Strange:  TODO:  verify! 

//eeprom char label2[]="pwrcycl:\0";
eeprom long int powercycles=0; 
					   // this initial value will be programmed by the PIC programmer only, 
 					   // this value can be read by a PIC programmer, 
 			         	   // it will give government, health-insurance or parents 
				           // valuable information about how frequently this game-system 
				           // has been used.

//eeprom char label3[]="errcount:\0";
eeprom long int errorcount=0; 
                                           // shows how many errors happened, serves as a primitve logfile to encourage investigation

//eeprom char label4[]="lasterr:\0";
eeprom long int lasterror=0; 
                                           // shows WHAT error happened, useful for debugging a crashed system

// more examples how to access the EEPROM:
// eeprom unsigned char inputData[3]={0xa,0xb,0xc};
// __EEPROM_DATA('W','E','L','C','O','M','E',' ');
 
long int resetlevel;     // during POST or crash the initialization-reason is stored here, so 
                    // after a POST-reset, WDT-reset or BOD-reset the initialization can be done partially 
                    // attempting to recover system states and user-data.
                    // http://ww1.microchip.com/downloads/en/devicedoc/reset.pdf

#define IINT    1   // INT  NOT USED:  humidity, light, shock, midnight, detected, punish user by sending high voltage to electrodes
#define IPD     2   // IPD  power was down, woke up from sleep by /MCLR reset (external), what went wrong, cold- or warm-boot?...
#define ITO     4   // ITO  WDT timeout program crashed for EMP or software reson, partial reset, try to keep system running *EXCEPT 12bit cores
#define IBOD    8   // BOD  brown-out-happened, data cannot be trusted, validation and conservative reinitialization
#define IPOR    16  // POR  this is a cold boot power on reset: all RAM data initialized, EEPROM data should be validated
#define ISTKUNF 32  // STKUNF stack was empty, this is hopeless, firmwareupgrade required.
#define ISTKFUL 64  // STKFUL stack was full, this is hopeless, firmwareupgrade required.

//
//  microchip bug workaround according to:  https://www.microchip.com/forums/m911404.aspx  for older xc8 versions.
//  https://microchipdeveloper.com/xc8:cause-of-reset
// 
//extern unsigned char __resetbits;     // redeclaration not necessary.
//extern bit __powerdown;
//extern bit __timeout;


//#define RUNLEVEL  // used  for more complex systems to enable/disable functionality due to partially unpopulated PCBs

int autofire; // leaving it uninitialized to keep system state after BOD or Watchdog crash. But during POST this should become 2A=off;
              //  7 bits for parity are used, I store INVERSE binary patterns so that validity can be checked
              //  every time after power-failure, POST, or WDT. these codes will increse robustness to avoid accidental firing.
	      //  the only two valid states:   55h = 1010101b = on,  2Ah = 0101010 = off  .  everything else will be reset to 2Ah.
              //  since a power-failure or data-curruption will cause autofire to become invalid, sanity-check will detect
              //  and set the code for OFF.  As long as the data code is valid, we can assume there was no corruption, and 
              //  so we can keept autofire ON even after system-crash.


// Prototypes

void InitPorts(void); 
int GetKey(int port); 			// 1 for pressed key,  port 0 or 1 for GP0, GP1 respectively, debouncing GetKey 
void PollKeysDuringDelay(int jiffies);  // one jiffy is 1/100th of a second by commodore standards... 
void Msg(int count);       		// show code as LED-flashes, beeps, 7/15 segment display, LCD, oLED 


// Main

void main(void)
{
  // determine reset reason of any existing PICmicro MCU.  use resetlevel as an abstraction layer, or get rid of it and 
  // talk to PCON, STATUS, CPUSTA, ... registers directly to keep the program-code short, and to increase stability.
  // using the XC8 compiler, init by the compiler must be avoided!  https://www.medo64.com/2015/01/detecting-watchdog-reset-in-xc8/
  // backup-reset-condition-flags must be selected in the linker.
  resetlevel=0;   // abstraction variable that will help me to keep the switch structure easier to deal with error combinations.


  //  I assume that TO is the same as __timeout that I am forced to use in the XC8 env.  i
  //   means that WDT detected a crash ( barked ) and caused a reset.
  if (__timeout==0) resetlevel |= ITO;        // if TO bit is clear, then resetlevel get ITO bit set.
  __timeout==1;                               // TO=set. prepare for next reset;  TODO test! I am not sure if i am allowed to write to this var.

//  if POR (=bit1) in the PCON register is zero, then it means we have a fresh start, so in this case: init autofire to be OFF, 
//  in all other cases recover and continue system doing what what it has been doing
  if ((0b11111101 && PCON)==0) resetlevel |=IPOR;
//  now set POR to one to confirm that we have finished handling this init-situation, and we are ready to detect it again the next time.
  PCON |= 0b00000010;         //load gun for the next incident.

//  if PD bit is zero, then it means we had a reset during sleep
  if (__powerdown==0) resetlevel |= IPD;        // if PD bit is clear, then resetlevel get IPD bit set.
  __powerdown==1;                               // PD=set. since we are out of sleep now, let's set it on, as a preparation.  TODO: debug

// check if BOD (=bit0) condition is given, also make sure that POR is not given.
  //if (((0b11111110 && PCON)==0) && ((0b00000001 || PCON)==0)) resetlevel |=IBOD;   I could never detect this situation.  What's wrong!
  if ((0b11111110 && PCON)==0) resetlevel |=IBOD;
//  now set BOD to one to confirm that we have finished handling this init-situation, and we are ready to detect it again the next time.
  PCON |= 0b00000001;         //load gun for the next incident.

// the datasheets contradicts this information:
// since BOR is unknown/or unreliable according to http://ww1.microchip.com/downloads/en/devicedoc/reset.pdf page 7
// we don't need to check for it, instead we assume that the restart-reason must have been brown-out.
// oliver's comment:  this didn't work will.  I even got BOD when pressing the reset button.  So I want to lower the detection-voltage-threshold.

  if (resetlevel==0) resetlevel = 13; //some nonsense has happenden, resetlevel shouldn't be zero.  analyze the value.  TODO  define something that looks nicer to distinguish, since I cannot display Msg(0);
  // Why are we booting?  POST?  WDT?  BOD?  or a cobination of them.  To keep it universal, the state will 
  // be recorded in this INIT-state variable and be referenced whenever needed!
  // This keeps the init procedure sequential and therefore easily readable, better than defining subroutines. 
  // if I read the doc correctly, it shouldn't happen that more than one bit is set.  We will test for this later and output an error.

/*
An /MCLR reset will not affect the TO, POR and BOR bits and can be detected by the absence of other reset causes indicated by these bits, if they were set appropriately before the reset.
*/


  // INIT
  powercycles++;   // this variable may be interesting for NSA or health insurance , TODO: string-message doesn't appear in front of it FIX it!

// TODO add and multiply up the bytes including my name  -> "Sorry no Zaxxon!". 
//  if checksum is wrong, then output MSG(EROM) , but try to start the device despite the error anyway.

  //for keeping debug information 
  lasterror=0;   // debugging 

  InitPorts();     // this port configuration is valid for all 3 resettypes:  Brownout, POST, watchdog

  // depending on  POST, WDT, BOD  execute only the initialization necessary to preserve user-data after a failure
#ifdef DEBUG
  Msg(resetlevel); // report the calculated resetlevel to developer, will be handled by "switch (resetlevel)" to manage every thinkable case.
#else
  resetlevel=IPOR; // force system into proper startup, or the speed cannot be programmed during other reset types.
#endif

  switch (resetlevel)
  {
       case IPOR:          // fresh system-start, so init autofire to be OFF
          autofire=0x2A;   // stop shooting only during POST, ( but don't exec this line during Brownout or Watchdog reset)

          // if STOP-key is pressed then SPEED--    execute only during POST 
          if(GetKey(STOP)) //If STOP Key Pressed during powerup then decrease autofire speed.
            {
               if(autofirespeed>MIN)
               {
                    autofirespeed--;
               }
            }

          // if START-key is pressed then SPEED++   execute only during POST
          if(GetKey(START)) //If START Key Pressed during powerup then increase autofire speed.
            {
               if(autofirespeed<MAX)
               {
                    autofirespeed++;
               }
             }

          Msg(autofirespeed);      // communicates the argument number (autofirespeed)  by flashing the LED

          //TODO press both buttons at POST? / during operation? to toggle SOUND on/off, store in EEPROM
          break;
       ;
       case IBOD:          // brown-out  system-start, so init autofire to be OFF  how cool, 
                           // I have tested it with a capacitor,  unplugging and plugging the power in too early causes this error!  
          // we do nothing to handle this situation, for the joystick autofire program, 
          // because we don't want to disturb the player playing.  
          // so we want to keep autofire on if it was on, and off if it was off.  corruption detection will handle this further down 
          // this is only useful for debugging purposes, and for future projects! 
          asm("nop");
          #ifdef DEBUG
             Msg(DEBUG19);		  	   // communicates DEBUG level 19 to developer by flashing the LED 19 times
          #endif
          break;
       default:                    // something weird has happened, and it should be reported as an error
          #ifdef DEBUG
             Msg(DEBUG25);		  	   // communicates DEBUG level 25 to developer by flashing the LED 25 times
          #endif
       ;
  }



/**********IF weird value, then report ERROR msg, increment EEprom error counter.
                                  // keep shooting during minor system problems.  but don't shoot during POST.
                    
 write POR to be prepared...
**************/
//
// slides about reset implementation
// http://ww1.microchip.com/downloads/en/devicedoc/reset.pdf

/*
9.3.7 POWER CONTROL (PCON) STATUS REGISTER
The power CONTROL/STATUS register, PCON (address 8Eh) has two bits.
Bit0 is BOD (Brown-out). BOD is unknown on Power- on Reset. It must then be set by the user and checked on subsequent RESETS to see if BOD = 0, indicating that a brown-out has occurred. The BOD STATUS bit is a don’t care and is not necessarily predictable if the brown-out circuit is disabled (by setting BODEN bit = 0 in the Configuration word).
Bit1 is POR (Power-on Reset). It is a ‘0’ on Power-on Reset and unaffected otherwise. The user must write a ‘1’ to this bit following a Power-on Reset. On a subsequent RESET, if POR is ‘0’, it will indicate that a Power-on Reset must have occurred (i.e., VDD may have gone too low).
*/


// but we don't need to worry about it, because if autofire is x00 or xFF, then it will be detected by the sanity-check and set to x2A

// RAM and EEPROM data sanity check, crash safetly measures ALWAYS execucuted (meaning: during POST, Watchdog, and Brownout!)
// disable autofire when detecting memory corruption, data stored with inverted parity in EEPROM  TODO:  also store in RAM

     if (
		((autofire != 0x2A) && (autofire != 0x55)) ||     // from RAM
		(autofirespeed>MAX) ||                            // from EEPROM
		(autofirespeed<MIN)
        )
     {
          autofire=0x2A;  	// stop shooting (state in RAM) because we detected corruption, this fixes the corrupt RAM value. 
          autofirespeed=MED;    // set to normal speed in EEPROM because we detected corruption, this fixes the corrupt EEPROM value.
          Msg(ECORRUPT);      	// 9 flashes to the User indicate detected RAM or EEPROM data corruption.  
                                // This is normal for the first power-on of a new device, and shouldn't happen after shipping.
				// TODO:  the Error codes could be definied more distinctively to improve customer service quality.
          lasterror=ECORRUPT;   // debugging 
          errorcount++;         // e.g. programmer can show this EEPROM location to show error statisics
     }



  // ENDLESS LOOP 

  while(1)
  {
      asm("clrwdt"); // TODO I can finetune the watchdog counter value to survive in a game while autofire is on.
      //      CLRWDT();    // reset Watch Dog Timer Macro.  if you don't, then the system will reset every 2 seconds.
      //      _asm clrwdt  _endasm;  // reset watchdog timer    BROKEN CODE


      if(autofire==0x55)  // on 
      {
      // output fire one pulse, one LED flash, one buzzer-beep

      // FIRST HALF
          // fire
          TRISIO5=0; // (green) switch driver ON to ouput the constant 0.
          GP5 = 0;   // FIRE pressed - seems to lose value after being in TRISTATE, value 0 means driving to 0 Volt:  LOW

          // output red LED
          GP4 = 0; //RED LED ON

          // output sound buzzer  or  vibration

          //__delay_ms(1000); //1 Second Delay
	  //PollKeysDuringDelay(100);
	  PollKeysDuringDelay(shoot[autofirespeed]);

      // SECOND HALF
          // fire
          TRISIO5=1; // (green) switch driver OFF and go into INPUT-mode to be passive/tristate:  TRI
          //GP5 = 1; // FIRE unpressed - it's forbidden to drive to HIGH=1 !!!, wirded-OR-short-circuit, so don't uncomment this line.

          // output red led
          GP4 = 1; //RED LED OFF

          // output sound buzzer 

	  //PollKeysDuringDelay(100);
	  PollKeysDuringDelay(reload[autofirespeed]);
      }
      else   // don't fire
      {
	  PollKeysDuringDelay(1);   // just wait a short time and check the keys
          // TODO:  improve power efficiency how much current during delay, how much during deep sleep?
          // TODO:  are symetrical gone shots better than asymetrical fires?
      }
  }
}





// functions 

void InitPorts(void) //this is a cold (POST) and warm (WDT) reset.  It restores the port configuration without initializing autofire
                     // so running InitPorts often will theoretically  make this microcontroller more stable.
{
  OPTION_REG = 0b01111111; // MSB=0 to enable all the pull-up resistors
  WPU        = 0b00000111; // configure weak pull-ups only for GP0 and GP1, because:
                           // TRIS in input mode shouldn't have pull-up, pullup will come from commodore 64 for fire-output port.
			   // but what about ESD?  maybe I need to add two protection diodes around the input 
  ANSEL      = 0b00000000; // disable analog input, configure bits 3-0 to be digital
  CMCON      = 0b00000111; // disable comparitor

  // TRIS is the data-direction-register   remember:  1nput  0utput 
  TRISIO0    = 1;          // GP0 INPUT  joystick-autofire-start-key-low-active
  TRISIO1    = 1;          // GP1 INPUT  joystick-autofire-stop-key-low-active

  // DRIVEN outputs  - necessary to avoid floating signals - e.g. if output is fed into logic-chips
  TRISIO4    = 0;          // GP4 OUTPUT tristate-wired-OR-low-active, this value will control the output
  GP4=1;                   // LED OFF (active-low, LED between 5V and GP4).  It's better to have it OFF than ON after WDT reset.

  // WIRED-OR  outputs  ( simulating Open Collector outputs )
  TRISIO5    = 1;          // GP5 OUTPUT tristate-wired-OR-low-active, this value will control the output
  GP5=0;                   // active LOW driver on, meaning: wired-OR-enabled, NOTE: THIS VALUE SHOULD NEVER BE 1 !!!
  // TODO XXXXXXX I don't wan to call it GP5, I want to call it GPIO bit FIRE
}


int GetKey(int port) //  returns 1 for pressed key,  input port 0 or 1 for GP0, GP1 respectively, debouncing GetKey 
{
   // Is GP(port) low - no so exit
   if (GPIO & (1<<port)) return 0; // key hasn't been pressed
   __delay_ms(1); // wait for key to settle

   // Is GP(port) high ? yes so exit = false key.
   if ( (GPIO & (1<<port))>0 ) return 0; // was a false key so restart
   __delay_ms(3); // wait for key to settle

   // Is GP(port) high ? yes so exit = false key.
   if ( (GPIO & (1<<port))>0 ) return 0; // was a false key so restart
   __delay_ms(2); // wait for key to settle

   // Is GP(port) high ? yes so exit = false key.
   if ( (GPIO & (1<<port))>0 ) return 0; // was a false key so restart

   return 1; // a keypress has been detected on port.
}

	  
// poll both user-keys ( START & STOP ) while waiting between upper or lower output pulses.
void PollKeysDuringDelay(int jiffies)  // one jiffy is 1/100th of a second by commodore standards... 
				       // https://en.wikipedia.org/wiki/Jiffy_(time)
{
   int i=0;
   for(i=0 ; i<jiffies ; i++)
   {
      // check keys and register new states
      if(GetKey(START)) //If start Key Pressed    TODO:  use interrupts, so you don't miss user input
          autofire=0x55;  // start shooting

      if(GetKey(STOP)) //If stop Key Pressed
          autofire=0x2A;  // stop shooting

          __delay_ms(16); // about or 7ms Delay, 10ms in total together with both GetKey debounce delays  TODO:why is __delay_ms off by factor 2 ?
          asm("clrwdt");   // during long delays >2s  it's importan't to reset the watch-dog-timer 
   }
}


void Msg(int count)       // show code as LED-flashes, beeps, 7/15 segment led-display, LCD, oLED 
   {
   int i=0;
   for(i=0 ; i<count ; i++)
      {
          GP4 = 0; //LED ON  TODO:  make sounds by using piezzo 
          __delay_ms(50); // 100ms Delay
          GP4 = 1; //LED OFF
          __delay_ms(150); // 200ms Delay
          asm("clrwdt");   // when flashing more than 4 times WDT would be activated to reset the system
      }
      __delay_ms(500); // 1s Delay  to indicate end-of-message and to allow user*Innen to take his/her hands of the key.
   }


