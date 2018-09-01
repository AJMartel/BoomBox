 /* D14 HUGE Bomb Prop Code v1.1
   Hardware has:
   1. 2 Red, 2 Blue, 2 white LED Strobes. (right and left) (6 DO)
   2. Air Horns switched by relay. (not powered by case) (1DO)
   3. LCD (I2C interface) 4x20 Blue.
   4. 4x3 Keypad (traditional matrix interface).
   5. Buzzer.
   6. Large Red Button.
   7. Siren.
   8. Compressor.
   9. Purge Valve.
   10. A3 input with voltage divider to measure batt voltage.
   11. Smoke Output.
   12. Can Sense Input (reed switch).
   13. MP3 Serial I/O.

   MODES:
   Pressing red button enters program mode during initialization. Battery voltaqe displayed during power up.
   1. Simple Countdown - Unit counts down and explodes unless red cancel button is pressed. pressing red button restarts.
   2. PIN-Countdown - A PIN (2-9 digits) starts countdown time and pauses. EOD Guess allows guessing PIN.
   3. Repeating Countdown - Unit counts down and explodes unless red reset button is pressed. Reset restarts countdown.
   Program Mode - Set game options and game modes. * exits programming and starts game.

   EOD Mode:
   1. EOD-Guess - Only for PIN countdown. Wrong PIN wont trigger, it gives you a Too High or Too Low response. * gives a +/- 200 number range hint.
   
   EEPROM Structure:
   0 - Default game mode.
   1 - Strobe during countdown(Bit 1), Beep during countdown (Bit 2), EOD Wire Guess Mode (Bit 3), BB Cannon Mode (Bit 4), Card Sense (Bit 5),
     Two Min Warning (Bit 6), Pre Countdown Beep (Bit 7), Can Sense (Bit 8)
   2 - Alert Duration (default 20s).
   3-4 - Countdown time (Seconds, 0-64000)
   5-8 - PIN (long)
   9 - Juke Box MP3 delay in minutes.
   10 - Main volume (1-30)
   11 - Two Minute Countdown Song (0=none, 1-254 for alternate)
*/
#include <EEPROM.h>
#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <DFPlayer_Mini_Mp3.h>
# define HWSERIAL Serial1
// RX1,RX2 - MP3 Serial DIO
const byte sirenPIN = 2; // HIGH = Siren on.
const byte compPIN = 3; // HIGH = Compressor powered.
const byte valvePIN = 4; // HIGH = Purge air.
const byte buzzPIN = 5; // HIGH = Piezo Buzzer on.
const byte hornPIN = 17; // HIGH = Ext relay on.
const byte brbPIN = 20; // LOW = Big Red Button pressed.
const byte cardPIN = 24; // LOW = Card inserted.
const byte canPIN = 22; // LOW = Canister inserted.
byte rowPins[4] = {6,7,8,9}; // connect to the row pinouts of the keypad
byte colPins[3] = {10,12,11}; // connect to the column pinouts of the keypad
char keys[4][3] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
const byte ledRedPIN = 13; // HIGH = Red LEDs on.
const byte ledBluePIN = 14; // HIGH = Blue LEDs on.
// SDA0, SCL0 - LCD I2C DIO (Pins 18,19)
const byte ledWhitePIN = 15; // HIGH = White LEDs on.
//const byte smokePIN = 16; // HIGH = White LEDs on. Not Used Currently.
const byte voltPIN = 9; // Voltage Measurement analong PIN. (Pin 23)
byte volMP3 = 23; // Volume of MP3 player, 0-30.
byte currMP3 = 100; // Current MP3 track.
byte mp3Top = 117; // Last MP3 file number.
byte twoMinMP3 = 36; // Song number to play on 2 minute countdown. 0=none.
char key;
int cdPeriod = 1200; // Number of seconds for countdown. 64000 - 17 h max
long lngPIN = 0; // The 1 to 9 digit PIN code needed for the game.
unsigned long timeOut = 0; // millis when we reach end of some important countdown.
unsigned long menuRefresh = 0; // Controls refresh rate for menu.
String lcdLine1 = ""; // Holds current LCD Line 1 text.
String lcdLine2 = ""; // Holds current LCD Line 2 text.
String lcdLine3 = ""; // Holds current LCD Line 3 text.
String lcdLine4 = ""; // Holds current LCD Line 4 text.
String gameNames[4] = {"Countdown","PIN Countdown","Rpt-Countdown","Can-Countdown"};
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, 4, 3 );
LiquidCrystal_I2C lcd(0x3F, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address, SDA0-18, SCL0-19
  
// Game Play Variables
bool cdBeep = false; // Countdown Beep enabled.
bool cdStrobe = false; // Countdown Strobe enabled.
bool canSense = false; // Canister sensing is not used when false.
bool bbCanon = false; // True if BB Cannon is enabled.
bool preCDBeep = false; // Beeps to help locate bomb on field before countdown or interaction starts.
bool twoMin = false; // True if 2 minute air raid warning is enabled.
bool eodMode = false; // Global to indicate EOD Guess Mode is active.
bool cardSense = false; // Global to indicate card presence sense is active.
bool played = false; // Indicates some MP3 file has already been played.
bool jukeMode = true; // if True, play audio, if false, play sys info files.
bool blnWhite = false; // Which LED is/was flashed, used to alternate flashes.
bool firstRun = true; // Global used to indicate first run thru a subroutine.
bool gameOn = true; // GameOn or Prog mode, or neither (device disabled). Default startup goes direct into a game.
byte x = 5; // Used for Canister Mode timing loops.
byte gameMode = 1; // Of the games we can play, this is the current mode.
byte lastgameMode = 1; // Stores the game mode for juke box mode to return to.
byte alertPeriod = 20; // 20s default alert period when triggered.
byte gameStep = 0; // What step in the game mode we are in. Games have different steps or sequences.
byte pinFail = 0; // Counts number of PIN entry failures.
byte songPeriod = 15; // Minutes between songs when idle. 0 = disabled.
long enteredPIN = 0; // What PIN code was entered by player.
unsigned long songTimer = 0; // how many millis till the next song.
unsigned long eodTimeout = 0; // Contains millis for end of eod bypass period (less than main countdown, 2 minutes max).
unsigned long nextCheck = 0; // How many millis till clock needs updating.
unsigned long nextEODCheck = 0; // How many millis till EOD clock needs updating (2 minute countdown vs main countdown clock).

// Programming Variables
byte configByte = 0; // Used to read config from EEPROM and in program mode sub.
bool progMode = false; // Game mode or Prog mode, or neither (device disabled).
bool optionShown = false; // Programming - did we show the current option state already?
bool progSelected = false; // Programming - did we choose what to change?
byte progMenu = 0; // What Program Option we should show.

void setup() {
  int voltRaw = 0; // input value for Analog voltage measurement.
  float pinVoltage = 0; // holds calc voltage for Analog voltage measurement.
  byte eepromHigh = 0; // Used to parse EEPROM reads.
  byte eepromLow = 0; // Used to parse EEPROM reads.

  // Set PinModes.
  pinMode(ledRedPIN, OUTPUT);
  digitalWrite(ledRedPIN,LOW);
  pinMode(ledBluePIN, OUTPUT);
  digitalWrite(ledBluePIN,LOW);
  pinMode(ledWhitePIN, OUTPUT);
  digitalWrite(ledWhitePIN,LOW);
  pinMode(hornPIN, OUTPUT);
  digitalWrite(hornPIN,LOW);
  pinMode(sirenPIN, OUTPUT);
  digitalWrite(sirenPIN,LOW);
  pinMode(buzzPIN, OUTPUT);
  digitalWrite(buzzPIN,LOW);
  pinMode(compPIN, OUTPUT);
  digitalWrite(compPIN,LOW);
  pinMode(valvePIN, OUTPUT);
  digitalWrite(valvePIN,LOW);
//  pinMode(smokePIN, OUTPUT);
//  digitalWrite(smokePIN,LOW);
  pinMode(voltPIN,INPUT); // Analog A9 voltage input
  pinMode(brbPIN, INPUT_PULLUP);
  pinMode(cardPIN, INPUT_PULLUP);
  pinMode(canPIN, INPUT_PULLUP);

//  Serial.begin(9600);
//  Serial.println("Starting...");    

  // Byte 0 - Get Default game mode from EEPROM
  gameMode = EEPROM.read(0);
  if(gameMode < 1 || gameMode > 4) {
    EEPROM.write(0,1);
    gameMode = 1;
  }
  // Byte 1 Get Config Byte from EEPROM
  configByte = EEPROM.read(1);
  if(configByte > 255) { // If its invalid, clear it.
    EEPROM.write(1,0);
    configByte = 0; 
  }
  if(configByte & 1) cdStrobe = true;
  if(configByte & 2) cdBeep = true;
  if(configByte & 4) eodMode = true;
  if(configByte & 8) bbCanon = true;
  if(configByte & 16) cardSense = true;
  if(configByte & 32) twoMin = true;  
  if(configByte & 64) preCDBeep = true;  
  if(configByte & 128) canSense = true;  
  // Byte 2 Get explosion Duration from EEPROM
  alertPeriod = EEPROM.read(2);
  if(alertPeriod < 1 || alertPeriod > 240) { // If its invalid, set to default.
    EEPROM.write(2,20);
    alertPeriod = 20;
  }
  // Byte 3,4 Get Countdown time (Seconds, 0-64000) from EEPROM
  eepromHigh = EEPROM.read(3);
  eepromLow = EEPROM.read(4);
  cdPeriod = word(eepromHigh,eepromLow);
  if(cdPeriod > 64000 || cdPeriod < 1) { // If its invalid, set to default.
    EEPROM.write(3,4);
    EEPROM.write(4,176);
    cdPeriod = 1200; // 1200 seconds
  }
  // Byte 5-8 Get PIN from EEPROM
  lngPIN = EEPROMReadLong(5);
  if(lngPIN < 1 || lngPIN > 999999999) { // If its invalid, set to default.
    lngPIN = 1234;
    EEPROMWriteLong(5,lngPIN);
  }
  // Byte 9 - Get MP3 Song delay in minutes from EEPROM
  songPeriod = EEPROM.read(9);
  if(songPeriod > 60) {
    EEPROM.write(9,15);
    songPeriod = 15;
  }
  // Byte 10 - Get main volume from EEPROM
  volMP3 = EEPROM.read(10);
  if(volMP3 > 30) {
    EEPROM.write(10,24);
    volMP3 = 24;
  }
  // Byte 11 - Get MP3 Song for 2 minute countdown from EEPROM
  twoMinMP3 = EEPROM.read(11);
  if(twoMinMP3 > mp3Top) {
    EEPROM.write(11,36);
    twoMinMP3 = 36;
  }
  randomSeed(analogRead(7));
  lcd.begin(20,4);   // Initialize the lcd for 16 chars 2 lines, turn on backlight
  lcd.backlight();
  HWSERIAL.begin(9600);
  mp3_set_serial (HWSERIAL); //set Serial for DFPlayer-mini mp3 module 
  delay(1000);
  mp3_set_volume (volMP3);
  mp3_set_EQ(0); // Specify EQ 0/1/2/3/4/5  Normal/Pop/Rock/Jazz/Classic/Bass
  digitalWrite(ledRedPIN,HIGH);
  voltRaw = analogRead(voltPIN);
  pinVoltage = voltRaw * 0.0212;       //  Calculate the voltage on the A/D pin 4.7k/2.2k = 0.015 multiplier
  if (pinVoltage < 12.0) {
    if (pinVoltage < 11.3) {
      mp3_play (3); //play Critical Battery MP3
    } else {
      mp3_play (2); //play Low Battery MP3
    }
  } else {
    mp3_play (23); //play Startup MP3
  }
  timeOut = millis() + 6000;
  lcdLine1=F("D14 Airsoft BoomBox");
  lcdLine2=F("Press the red button");
  lcdLine3=F("to program.");
  lcdLine4=F("FW Version 1.1
  ");
  while(millis() < timeOut){ // For 6 seconds after power up, wait for BRB press.
    if ((timeOut - 4000) < millis()) {
        digitalWrite(ledRedPIN,LOW);
        delay(20);
        digitalWrite(ledRedPIN,HIGH);
        delay(20);
    }
    if ((timeOut - 2200) < millis()) {
        lcdLine4 = "Battery: " + String(pinVoltage);
        if (pinVoltage < 12) {
          lcdLine4 = F("LOW BATTERY!");
        }
        digitalWrite(ledBluePIN,LOW);
        delay(20);
        digitalWrite(ledBluePIN,HIGH);
        delay(20);
    }
    if((digitalRead(brbPIN) == LOW) && ((timeOut - 1000) < millis()) ) { // If BRB is pressed, enter program mode.
      lcdLine4=F("Entering prog mode.");
      progMode = true;
      gameOn = false;
      timeOut = millis();
      break;
    }
    if ((timeOut - 1300) < millis()) {
        digitalWrite(ledWhitePIN,LOW);
        delay(20);
        digitalWrite(ledWhitePIN,HIGH);
        delay(20);
    }
    if (menuRefresh < millis()) {
      writeLCD(0);
      menuRefresh = millis() + 1000;
    }
  }
  digitalWrite(ledWhitePIN,LOW);
  digitalWrite(ledRedPIN,LOW);
  digitalWrite(ledBluePIN,LOW);
  clearLCD();
}

void loop() {
  if(progMode) programMode();
  if(gameOn) {
    if (firstRun) {
      clearLCD();
      key = keypad.getKey();
      if (key != NO_KEY) { // Check for key and process.
        if (key == '#') {
          lastgameMode = gameMode;
          gameMode = 5;
          // Set up the LCD
          lcdLine1 = F("1=Music/Info        ");
          lcdLine2 = F("4=Prev        6=Next");
          lcdLine3 = F("7=Vol+        9=Stop");
          lcdLine4 = F("*=Vol-        #=EXIT");
          writeLCD(0);
          currMP3 = 1;
        }
      }
      if (!played) {
        played = true;
        switch (gameMode) {
          case 1:
            currMP3 = 9;
            lcdLine1 = F("Simple Countdown.");
          break;
          case 2:
            currMP3 = 6;
            lcdLine1 = F("PIN Locked Countdown");
          break;
          case 3:
            currMP3 = 18;
            lcdLine1 = F("Repeating Countdown");
          break;
          case 4:
            currMP3 = 12;
            lcdLine1 = F("Canister Countdown");
          break;
          case 5:
            currMP3 = 5;
            lcdLine1 = F("Jukebox Mode");
          break;
        }
        writeLCD(0);
        mp3_set_volume (volMP3);
        mp3_play(currMP3);
        if ((bbCanon) && (gameMode != 5)) {
          lcdLine2 = F("BB Cannon is ACTIVE!");
          lcdLine3 = "";
          lcdLine4 = F("USE EYE PROTECTION!");
        }
        writeLCD(4000);
        if ((bbCanon) && (gameMode != 5)) {
          mp3_set_volume (23);
          digitalWrite(ledRedPIN,HIGH); // Red LED On
          mp3_play(14);
          delay(11000);
          mp3_set_volume (volMP3);
          digitalWrite(ledRedPIN,LOW); // Red LED On
          clearLCD();
        }
      }
      firstRun = false;
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 1000;
      gameStep = 0;
      played = false;
    } else {
      switch (gameMode) {
        case 1: //Simple countdown
          simpleCD(false);
          break;
        case 2: //PIN countdown
          pinCountdown();
          break;
        case 3: //Repeating countdown
          simpleCD(true);
          break;
        case 4: //Canister countdown
          canCD();
          break;
        case 5: //Jukebox
          jukeBox();
          break;
      }
    }
  }
  if ((!gameOn) && (!progMode) && (songPeriod != 0)) {
    if (songTimer == 0) {
      songTimer = millis() + (songPeriod * 60000);
    }
    if (songTimer < millis()) {
      // Time for a song!
      int rndSong = random(100,mp3Top);
      mp3_set_volume (volMP3);
      mp3_play (rndSong); 
      songTimer = millis() + (songPeriod * 60000);
      lcdLine3 = F("Random MP3 play mode");
      lcdLine4 = F("Press button to stop");
      writeLCD(0);
    }
    if (digitalRead(brbPIN) == LOW) {
      mp3_stop();
    }
    key = keypad.getKey();
    if (key != NO_KEY) { // Check for key and process.
      switch (key) {
       case '4':
          // Next.
          mp3_next();
          break;
       case '7':
          // Vol Up.
          if (volMP3 > 29) {
            volMP3 = 30;
          } else {
            volMP3 ++;  
          }
          mp3_set_volume (volMP3);
          break;
       case '*':
          // Vol Down.
          if (volMP3 < 1) {
            volMP3 = 0;
          } else {
            volMP3 --;
          }
          mp3_set_volume (volMP3);
          break;      
        }
     delay(200);
    }
  }
}

void jukeBox() {
  // Music play form internal SD card.
  if (!played) {
    played = true;
    mp3_play (5); // Play jukebox MP3.
    delay(3000);
  }
  // Check for input
  key = keypad.getKey();
  if (key != NO_KEY) { // Check for key and process.
    byte mp3Upper = mp3Top; 
    byte mp3Lower = 100; 
    String tmpString = "";
    // Act on input.
    if (jukeMode) {
      mp3Upper = mp3Top;
      mp3Lower = 100;
    } else {
      mp3Upper = 36;
      mp3Lower = 1;
    }
    switch (key) {
       case NO_KEY:
          break;
       case '1':
          if (jukeMode) {
            jukeMode = false;
            currMP3 = 1;
          } else {
            jukeMode = true;
            currMP3 = 100;
          }
          break;
       case '2':
          mp3_set_EQ(3);
          break;
       case '5':
          mp3_set_EQ(2);
          break;
       case '8':
          mp3_set_EQ(4);
          break;
       case '0':
          mp3_set_EQ(0);
          break;
       case '4':
          // Back one song.
          if (currMP3 < (mp3Lower+1)) {
            currMP3 = mp3Lower;
          } else {
            currMP3 --;  
          }
          tmpString = String(currMP3);
          lcdLine1 = "1=Music/Info     " + tmpString;
          writeLCD(0);
          mp3_play(currMP3);
          break;
       case '6':
          // Next Song.
          if (currMP3 > (mp3Upper-1)) {
            currMP3 = mp3Upper;
          } else {
            currMP3 ++;  
          }
          tmpString = String(currMP3);
          lcdLine1 = "1=Music/Info     " + tmpString;
          writeLCD(0);
          mp3_play(currMP3);
          break;
       case '7':
          // Vol Up.
          if (volMP3 > 29) {
            volMP3 = 30;
          } else {
            volMP3 ++;  
          }
          mp3_set_volume (volMP3);
          break;
       case '9':
          // Stop.
          mp3_stop();
          break;
       case '*':
          // Vol Down.
          if (volMP3 < 1) {
            volMP3 = 0;
          } else {
            volMP3 --;
          }
          mp3_set_volume (volMP3);
          break;
       case '#':
          // Exit.
          mp3_stop();
          gameMode = lastgameMode;
          firstRun = true;
          played = false;
          break;
    }     
  }
}

void pinCountdown() {
  if (gameStep == 0) {
    // Initialize and ask for PIN
    clearLCD();
    digitalWrite(compPIN,LOW); // turn off compressor.
    lcdLine1 = F("PIN Locked Countdown");
    if ((canSense) && (digitalRead(canPIN) == LOW)) {
      // Can Sense in PIN game means it disables system. So we DONT want to start with it present.
      lcdLine2 = F("Can MUST BE REMOVED");
      writeLCD(0);
      beep(500);
      while (digitalRead(canPIN) == LOW) {
        lcdLine3 = F("Waiting...");
        writeLCD(500);
      }
    }
    if ((cardSense) && (digitalRead(cardPIN) == LOW)) {
      // Card Sense in PIN game means it disables system. So we DONT want to start with it present.
      lcdLine2 = F("Card MUST BE REMOVED");
      writeLCD(0);
      beep(500);
      mp3_play(31); // play card error.
      while (digitalRead(cardPIN) == LOW) {
        lcdLine3 = F("Waiting...");
        writeLCD(500);
      }
      lcdLine2 = "Enter PIN to start:";
      lcdLine3 = "";
    } else {
      lcdLine2 = F("Enter PIN to start:");
    }
    writeLCD(0);
    if (!played) {
      played = true;
      mp3_play (8); // Play enter PIN MP3.
      nextCheck = millis() + 10000;
      if (preCDBeep) {
        lcdLine3 = F("Locator Beep ON");
        writeLCD(0);
      }
    }
    enteredPIN = getLong(1,999999999, 3);
    if (enteredPIN == lngPIN) {
      gameStep = 1;
      preCDBeep = false;
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      lcdLine2 = F("Enter PIN to pause:");
      lcdLine3 = "";
      enteredPIN = 0;
    } else {
      lcdLine3 = F("INCORRECT PIN!");
      writeLCD(0);
      digitalWrite(ledRedPIN,HIGH);
      digitalWrite(sirenPIN,HIGH); // Siren On
      delay(200);
      digitalWrite(sirenPIN,LOW); // Siren Off
      digitalWrite(ledRedPIN,LOW);
      delay(1000);
    }
  }
  if (gameStep == 1) { // Countdown, check for pin or if EOD is allowed.
    played = false;
    if (nextCheck < millis() ) {
      nextCheck = checkTimer(timeOut);
      altStrobe();
    }
    // Check for disable card or can
    delay(2);
    if ((cardSense) && (digitalRead(cardPIN) == LOW)) {
       disableEnd();
    }
    if ((canSense) && (digitalRead(canPIN) == LOW)) {
       disableEnd();
    }
    key = keypad.getKey();
    if (key != NO_KEY) { // Check for key and process.
      switch (key) {
         case NO_KEY:
            break;
         case '0': case '1': case '2': case '3': case '4':
         case '5': case '6': case '7': case '8': case '9':
            beep(30);
            enteredPIN = enteredPIN * 10 + (key - '0');
            lcdLine3 = String(enteredPIN);
            writeLCD(0);
            break;
         case '*':
            beep(30);
            if (eodMode && enteredPIN == 0) {
              lcdLine3 = String(lngPIN+random(200)) + "-" + String(lngPIN-random(200));
            } else {
              lcdLine3 = "";
            }
            writeLCD(0);
            enteredPIN = 0;
            break;
         case '#':
            // CHeck for valid PIN
            if (enteredPIN == lngPIN) {
              gameStep = 2;
              cdPeriod = (timeOut - millis()) / 1000; // Set new cdPeriod to remaining time.
            } else {
              if (eodMode) {
                if (enteredPIN > lngPIN ) {
                  lcdLine3 = F("PIN too high.");
                } else {
                  lcdLine3 = F("PIN too low.");
                }
              } else {
                lcdLine3 = F("INCORRECT PIN!");
                writeLCD(0);
                pinFail ++;
                if (pinFail > 2) {
                  speedDet();
                }
              }
              digitalWrite(ledRedPIN,HIGH);
              digitalWrite(sirenPIN,HIGH); // Siren On
              delay(200);
              digitalWrite(sirenPIN,LOW); // Siren Off
              digitalWrite(ledRedPIN,LOW);
              delay(1000);
              enteredPIN = 0;
            }
            break;      
      }
    }
    if (nextCheck == 0) { // Time to detonate.
      detonate();
    }
  }
  if (gameStep == 2) {
    // Countdown paused.
    digitalWrite(compPIN,LOW); // turn off compressor.
    lcdLine2 = F("Enter PIN to restart");
    lcdLine3 = "";
    writeLCD(0);
    digitalWrite(ledWhitePIN,LOW);
    enteredPIN = getLong(1,999999999, 3);
    if (enteredPIN == lngPIN) {
      gameStep = 1;
      pinFail = 0;
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      lcdLine2 = F("Enter PIN to pause:");
      lcdLine3 = "";
      enteredPIN = 0;
    } else {
      lcdLine2 = F("INCORRECT PIN.");
      writeLCD(0);
      digitalWrite(ledRedPIN,HIGH);
      digitalWrite(sirenPIN,HIGH); // Siren On
      delay(200);
      digitalWrite(sirenPIN,LOW); // Siren Off
      digitalWrite(ledRedPIN,LOW);
      delay(1000);
    }
  }
}

void simpleCD(bool repeating) {
  if (digitalRead(canPIN) == LOW) {
    digitalWrite(ledRedPIN,HIGH);
  } else {
    digitalWrite(ledRedPIN,LOW);
  }
  if (gameStep == 0) {
    if ((cardSense) && (digitalRead(cardPIN) == LOW)) {
      // Card Sense in PIN game means it disables system. So we DONT want to start with it present.
      lcdLine2 = F("Card MUST BE REMOVED");
      writeLCD(0);
      beep(500);
      mp3_play(31); // play card error.
      while (digitalRead(cardPIN) == LOW) {
        lcdLine3 = F("Waiting...");
        writeLCD(500);
      }
    }
    if ((canSense) && (digitalRead(canPIN) == LOW)) {
      // Can Sense in PIN game means it disables system. So we DONT want to start with it present.
      lcdLine2 = F("Can MUST BE REMOVED");
      writeLCD(0);
      beep(500);
      while (digitalRead(canPIN) == LOW) {
        lcdLine3 = F("Waiting...");
        writeLCD(500);
      }
    }
    if (repeating) {
      lcdLine1 = F("");
      lcdLine2 = F("Red button restarts");
    } else {
      lcdLine1 = F("");
      lcdLine2 = F("Red button pauses");
    }
    if (!played) {
      played = true;
      mp3_play(11); // Play button press MP3.
    }
    writeLCD(1000);
    lcdLine2 = F("Press red button");
    lcdLine3 = F("to start count.");
    writeLCD(0);
    gameStep = 1;
  }
  if (gameStep == 1) { // waiting for start.
    played = false;
    if(digitalRead(brbPIN) == LOW) {
      // Start this mode.
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      gameStep = 2;
      beep(250);
      delay(1000);
      if (repeating) {
        lcdLine1 = "Repeating Countdown";
        lcdLine2 = F("Red button restarts");
      } else {
        lcdLine1 = "Simple Countdown";
        lcdLine2 = F("Red button pauses");
      }
      lcdLine3 = "";
    }
  }
  if (gameStep == 2) {
    if (nextCheck < millis() ) {
      nextCheck = checkTimer(timeOut);
      altStrobe();
    }
    delay(2);
    // Check for card or can disable.
    if ((cardSense) && (digitalRead(cardPIN) == LOW)) {
       disableEnd();
    }
    if ((canSense) && (digitalRead(canPIN) == LOW)) {
       disableEnd();
    }    
    if(digitalRead(brbPIN) == LOW) { // If BRB is pressed, hold or reset countdown.
      if (repeating) {
        beep(30);
        delay(250);
        beep(30);
        timeOut = millis() + (long(cdPeriod) * 1000);
        nextCheck = millis() + 10;
        mp3_stop();
      } else {
        gameStep = 3; // paused
        digitalWrite(ledWhitePIN,LOW);
      }
    }
  }
  if (gameStep == 3) {
    lcdLine1 = F("Countdown paused");
    lcdLine2 = F("Red button restarts");
    writeLCD(0);
    digitalWrite(ledWhitePIN,LOW);
    delay(1000);
    cdPeriod = (timeOut - millis()) / 1000; // Set new cdPeriod to remaining time.
    gameStep = 4;
  }
  if (gameStep == 4) {
    if(digitalRead(brbPIN) == LOW) { // If BRB is pressed, continue countdown.
      gameStep = 2;
      lcdLine1 = F("Countdown is");
      lcdLine2 = F("restarting...");
      writeLCD(0);
      beep(500);
      delay(500);
      if (repeating) {
        lcdLine1 = "Repeating Countdown";
        lcdLine2 = F("Red button restarts");
      } else {
        lcdLine1 = "Simple Countdown";
        lcdLine2 = F("Red button pauses");
      }
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
    }
  }
  if (nextCheck == 0) { // Time to detonate.
    detonate();
  }
}

void canCD() {
  lcdLine1 = F("Canister Countdown");
  // Canister game mode.
  if (digitalRead(canPIN) == LOW) {
    digitalWrite(ledRedPIN,HIGH);
  } else {
    digitalWrite(ledRedPIN,LOW);
  }
  if (gameStep == 0) { // Remove can if present.
    if ((cardSense) && (digitalRead(cardPIN) == LOW)) {
      // Card Sense in PIN game means it disables system. So we DONT want to start with it present.
      lcdLine2 = F("Card MUST BE REMOVED");
      writeLCD(0);
      beep(500);
      mp3_play(31); // play card error.
      while (digitalRead(cardPIN) == LOW) {
        lcdLine3 = F("Waiting...");
        writeLCD(500);
      }
    }
    if (digitalRead(canPIN) == LOW) {
      lcdLine1 = F("Canister Countdown");
      lcdLine2 = F("Please remove the");
      lcdLine3 = F("canister.");
      mp3_play(33); // Play remove can MP3
      writeLCD(0);
      while (digitalRead(canPIN) == LOW) {
        delay(100);
      }
    } else {
      gameStep = 1;
    }
  }
  if (gameStep == 1 ) { // Presss red button to start game.
    if (digitalRead(brbPIN) == HIGH) {
      lcdLine2 = F("Press the red button");
      lcdLine3 = F("to start game.");
      writeLCD(0);
      while (digitalRead(brbPIN) == HIGH) {
        delay(100);
      }
    } else {
      gameStep = 2;
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      lcdLine2 = F("Ready to Charge!");
      lcdLine3 = F("Insert Canister.");
      writeLCD(0);
      x = 5;
    }
  }
  if (gameStep == 2) {
    if (digitalRead(canPIN) == LOW) {   // Canister was attached. Start charging.
      if (x > 0) {
        lcdLine2 = F("Priming. Do Not");
        lcdLine3 = "disturb! (" + String(x) + ")";
        beep(20);
        digitalWrite(ledRedPIN,HIGH);
        writeLCD(1000);
        digitalWrite(ledRedPIN,LOW);
        writeLCD(1000);
        x--;
      } else {
        mp3_play(34); // Play primed MP3
        lcdLine2 = F("Hold red button to");
        lcdLine3 = F("charge device.");
        writeLCD(0);
        beep(500);
        gameStep = 3;
        x = 5;
      }
    }
  }
  if (gameStep == 3) {
    if ((digitalRead(brbPIN) == LOW) && (digitalRead(canPIN) == LOW)) {
      if (x > 0) {
        lcdLine2 = F("Charging, hold");
        lcdLine3 = "button... (" + String(x) + ")";
        beep(20);
        digitalWrite(ledRedPIN,HIGH);
        writeLCD(1000);
        digitalWrite(ledRedPIN,LOW);
        writeLCD(1000);
        x--;
      } else {
        lcdLine1 = F("Device Charged!");
        lcdLine2 = F("Detach Canister and");
        lcdLine3 = F("release button to");
        lcdLine4 = F("start countdown.");
        mp3_play(33); // Play remove can MP3
        beep(1000);
        writeLCD(0);
        while ((digitalRead(brbPIN) == LOW) || (digitalRead(canPIN) == LOW)) {
          delay(100);
        }
        gameStep = 4;
      }
    }
  }
  if (gameStep == 4) {
    if (digitalRead(canPIN) == HIGH) { // Can detached, countdown started.
      lcdLine1 = F("Canister Countdown");
      lcdLine2 = F("Device LOCKED!");
      lcdLine3 = F("Use Can to disarm.");
      writeLCD(995);
      timeOut = millis() + (long(cdPeriod) * 1000);
      nextCheck = millis() + 10;
      gameStep = 5;
      x = 5;
    }
  }
  if (gameStep == 5) {
    if (nextCheck < millis() ) {
      nextCheck = checkTimer(timeOut);
      if (eodTimeout > 0) {
        nextEODCheck = checkEODTimer(eodTimeout);
      }
      altStrobe();
    }
    if (nextCheck == 0) { // Time to detonate.
      detonate();
    }
    if (eodMode) {
      if (eodTimeout == 0) { // no EOD timeout set, assume its starting
        if (digitalRead(canPIN) == HIGH && digitalRead(brbPIN) == LOW) { // No can present, Red Btn Pressed, time to start countdown (4 mins).
          eodTimeout = millis() + 240000;
          nextEODCheck = millis() + 1000;
          if (eodTimeout > timeOut) {
            // its too late, detonate with malice!
            speedDet();
          }
        }
      } else { // eod Timeout already started. verify button and no can
        if (digitalRead(canPIN) == HIGH && digitalRead(brbPIN) == LOW) { // no can and buton is still pressed, increment eod display line
          if (nextEODCheck == 0) {
            // Bypassed!
            lcdLine1 = "EOD Bypass";
            lcdLine2 = "success!";
            beep(1000);
            writeLCD(0);
            disableEnd();
          } // No else here cause BTn is held, no can, EOD time left, so do nothing and continue countdown..
        } else {
          // if button isnt pressed or can is attached, turn off eod countdown
          eodTimeout = 0;
          lcdLine1 = F("Canister Countdown");
          lcdLine2 = F("Device LOCKED!");
          lcdLine3 = F("Use Can to disarm.");
        }
      }
    }
    if (digitalRead(canPIN) == HIGH) {
      // Can reattached, tell em to hold button
      lcdLine2 = F("Press red button.");
      lcdLine2 = F("Device LOCKED!");
      lcdLine3 = F("Use Can to disarm.");
//      x = 5;
    }
    if (digitalRead(canPIN) == LOW && digitalRead(brbPIN) == HIGH) {
      // Can reattached, tell em to hold button
      lcdLine2 = F("Press Red Button!");
      x = 5;
    }
    if (digitalRead(cardPIN) == LOW && cardSense) { // card test for immediate disable.
      lcdLine2 = "Device Hacked!";
      lcdLine3 = "";
      lcdLine4 = "";
      writeLCD(500);
      disableEnd();
    }
    if (digitalRead(canPIN) == LOW && digitalRead(brbPIN) == LOW) {
      if (x > 0) {
        lcdLine1 = F("Discharging. Hold");
        lcdLine2 = "button (" + String(x) + ")";
        lcdLine3 = "";
        lcdLine4 = "";
        beep(20);
        digitalWrite(ledRedPIN,HIGH);
        writeLCD(2000);
        digitalWrite(ledRedPIN,LOW);
        writeLCD(2000);
        x--;
      } else {
        disableEnd();
      }
    }
  }
}

void disableEnd () {
  int x;
  digitalWrite(compPIN,LOW); // turn off Compressor.
  clearLCD();
  lcdLine1 = F("Device has been");
  lcdLine2 = F("DISABLED.");
  mp3_play (20);
  writeLCD(0);
  // accellerate countdown
  for (x=0;x<3;x++) {
    digitalWrite(ledBluePIN,HIGH); // Blue LED On
    digitalWrite(sirenPIN,HIGH); // Siren On
    delay(100);
    digitalWrite(ledBluePIN,LOW); // Blue LED Off
    digitalWrite(sirenPIN,LOW); // Siren Off
    delay(500);
  }
  digitalWrite(hornPIN,LOW); // Horn Off
  gameOn = false; // End this session.
  digitalWrite(ledRedPIN, LOW); // red led off.
  mp3_play (21);
  currMP3 = 100;
}

void speedDet() {
    mp3_play (16); //play Tampering MP3
    int remaining = (timeOut - millis())/1000;
    int x;
    if (remaining > 40) {
      remaining = 40;
    }
    lcdLine1 = F("Have a nice life");
    lcdLine2 = F("All 30sec of it");
    writeLCD(1000);
    // accellerate countdown
    for (x=0;x<(remaining/3);x++) {
      lcdLine1 = String(remaining - x);
      lcdLine2 = F("RUN.");
      writeLCD(0);
      digitalWrite(ledBluePIN,HIGH); // Blue LED On
      beep(100);
      digitalWrite(ledBluePIN,LOW); // Blue LED Off
      delay(100);
    }
    digitalWrite(ledBluePIN,HIGH); // Blue LED On
    for (;x<(remaining*.66);x++) {
      lcdLine1 = String(remaining - x);
      lcdLine2 = F("RUN. FASTER.");
      writeLCD(0);
      digitalWrite(ledRedPIN,HIGH); // Red LED On
      beep(50);
      digitalWrite(ledRedPIN,LOW); // Red LED On
      delay(50);
    }
    digitalWrite(ledRedPIN,HIGH); // Red LED On
    for (;x<remaining;x++) {
      lcdLine1 = String(remaining - x);
      lcdLine2 = F("RUN. FASTER. NOW!");
      writeLCD(0);
      digitalWrite(ledWhitePIN,HIGH); // White LED On
      beep(10);
      digitalWrite(ledWhitePIN,LOW); // White LED Off
      delay(10);
    }
    detonate();
}

void detonate() {
    bool hornOn = false;
    digitalWrite(compPIN,LOW); // turn off compressor.
    clearLCD();
    lcdLine1 = F("DEVICE DETONATED!");
    writeLCD(0);
    mp3_set_volume (30);
    mp3_play (27); // Play detonated mp3.
    digitalWrite(ledRedPIN,HIGH); // Red LED On
    digitalWrite(ledBluePIN,HIGH); // Blue LED On
    digitalWrite(ledWhitePIN,HIGH); // White LED On
    digitalWrite(hornPIN,HIGH); // Horn On
    digitalWrite(sirenPIN,HIGH); // Siren On
    hornOn = true;
    if (bbCanon) {
      digitalWrite(valvePIN,HIGH); // turn on valve, PURGE.
    }
    timeOut = millis() + (long(alertPeriod) * 1000); // set end of alert period
    nextCheck = millis() + 1000; // next check in 1 sec
    while (millis() < timeOut) {
      // set up 1sec on 1 sec off alternating..
      if (nextCheck < millis()) {
        nextCheck = millis() + 1000;
        if (hornOn) {
          digitalWrite(hornPIN,LOW);
          digitalWrite(ledRedPIN,HIGH); // Red LED On
          digitalWrite(ledBluePIN,HIGH); // Blue LED On
          digitalWrite(ledWhitePIN,HIGH); // White LED On
          hornOn = false;
        } else {
          digitalWrite(hornPIN,HIGH);
          digitalWrite(ledRedPIN,LOW); // Red LED Off
          digitalWrite(ledBluePIN,LOW); // Blue LED On
          digitalWrite(ledWhitePIN,LOW); // White LED On
          hornOn = true;
        }
      }
    }
    digitalWrite(ledRedPIN,LOW); // Red LED Off
    digitalWrite(ledBluePIN,LOW); // Blue LED Off
    digitalWrite(ledWhitePIN,LOW); // White LED Off
    digitalWrite(hornPIN,LOW); // Horn Off
    digitalWrite(sirenPIN,LOW); // Siren Off
    digitalWrite(valvePIN,LOW); // turn OFF valve.
    gameOn = false; // End this session.
    currMP3 = 100;
    mp3_set_volume (volMP3);
}

long checkEODTimer(unsigned long endmillis) {
  // increment display and check timer to verify its hasnt ended. return next millis to calling routine or 0 to indicate end.
  if (endmillis > millis() ) {
    // calc difference in time from millis to end millis
    unsigned long remaining = endmillis - millis();
    int hours = int(remaining/3600000);
    if (hours >0) {
      remaining = remaining - (hours * 3600000);
    } else {
      hours = 0;
    }
    int mins = int(remaining/60000);
    if (mins >0) {
      remaining = remaining - (mins * 60000);
    } else {
      mins = 0;
    }
    int secs = int(remaining/1000);
    twoMinCheck();
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(lcdLine1);
    lcd.setCursor(0,1);
    lcd.print(lcdLine2);
    lcd.setCursor(0,2);
    lcd.print("EOD Bypass:");
    lcd.setCursor(0,3);
    if (hours < 10) lcd.print("0");
    lcd.print(String(hours) + ":");
    if (mins < 10) lcd.print("0");
    lcd.print(String(mins) + ":");
    if (secs < 10) lcd.print("0");
    lcd.print(String(secs));
    lcd.print(" to halt");
    return(millis() + 1000);
  } else { // time expired
    return(0);
  }
}

long checkTimer(unsigned long endMillis) {
  // increment display and check timer to verify its hasnt ended. return next millis to calling routine or 0 to indicate end.
  if (endMillis > millis() ) {
    // calc difference in time from millis to end millis
    unsigned long remaining = endMillis - millis();
    int hours = int(remaining/3600000);
    if (hours >0) {
      remaining = remaining - (hours * 3600000);
    } else {
      hours = 0;
    }
    int mins = int(remaining/60000);
    if (mins >0) {
      remaining = remaining - (mins * 60000);
    } else {
      mins = 0;
    }
    int secs = int(remaining/1000);
    twoMinCheck();
    lcd.clear();
    lcd.setCursor(0,3);
    if (hours < 10) lcd.print("0");
    lcd.print(String(hours) + ":");
    if (mins < 10) lcd.print("0");
    lcd.print(String(mins) + ":");
    if (secs < 10) lcd.print("0");
    lcd.print(String(secs));
    lcd.setCursor(0,0);
    lcd.print(lcdLine1);
    lcd.setCursor(0,1);
    lcd.print(lcdLine2);
    lcd.setCursor(0,2);
    lcd.print(lcdLine3);
    return(millis() + 1000);
  } else { // time expired
    return(0);
  }
}

void twoMinCheck() {
  if (twoMin) { // If twomin is enabled give 2 min warning. cant put it in check sub cause it isnt called enough.
    if ( (millis() > (timeOut - 125000)) && (millis() < (timeOut - 124050)) ) {
      digitalWrite(ledBluePIN,HIGH);
      mp3_set_volume (25);
      mp3_play(17); // Play two min warning
    }
    if ( (millis() > (timeOut - 119950)) && (millis() < (timeOut - 119000)) ) {
      digitalWrite(ledBluePIN,LOW);
      mp3_set_volume (volMP3);
      if (twoMinMP3 != 0 ) { // Play two minute MP3 if selected.
        mp3_play(twoMinMP3);
      }
    }
  }
  if (bbCanon) { // If bbCanon is enabled start pump 30 secs before end.
    if (millis() > (timeOut - 30000)) {
      digitalWrite(compPIN,HIGH); // Turn on compressor
      digitalWrite(valvePIN,LOW); // turn off valve.
    }
  }
}

void programMode() {
  if(firstRun) {
    if (!played) {
      mp3_play (4); //play Program MP3
      played = true;
    }
    lcdLine1 = F("Program Mode. Press ");
    lcdLine2 = F("0 to scroll through,");
    lcdLine3 = F("# to select, and    ");
    lcdLine4 = F("* to exit.          ");
    writeLCD(0);
    firstRun = false;
  } else {
    key = keypad.getKey();
  }
  if(key != NO_KEY) {
    beep(30);
    if (key == '0') {
      progMenu ++;
      if (progMenu > 15) {
        progMenu = 1;
      }
      clearLCD();
      lcdLine3=F("Press # to change.");
      switch (progMenu) {
        case 1:
          lcdLine1=F("Current game:");
          lcdLine2=gameNames[gameMode-1];
          break;
        case 2:
          lcdLine1=F("Pre-Countdown locate");
          if (preCDBeep) {
            lcdLine2 = F("is enabled.");
          } else {
            lcdLine2 = F("is disabled.");
          }
          break;
        case 3:
          lcdLine1=F("Countdown strobe");
          if (cdStrobe) {
            lcdLine2 = F("is enabled.");
          } else {
            lcdLine2 = F("is disabled.");
          }
          break;
        case 4:
          lcdLine1=F("Countdown beep");
          if (cdBeep) {
            lcdLine2 = F("is enabled.");
          } else {
            lcdLine2 = F("is disabled.");
          }
          break;
        case 5:
          lcdLine1=F("Countdown time:");
          lcdLine2= String(cdPeriod/60) + " minutes.";
          break;
        case 6:
          lcdLine1=F("EOD Mode");
          if (eodMode) {
            lcdLine2 = F("is enabled.");
          } else {
            lcdLine2 = F("is disabled.");
          }
          break;
        case 7:
          lcdLine1=F("Canister sense");
          if (canSense) {
            lcdLine2 = F("is enabled.");
          } else {
            lcdLine2 = F("is disabled.");
          }
          break;
        case 8:
          lcdLine1=F("Card Sense is");
          if (cardSense) {
            lcdLine2 = F("enabled.");
          } else {
            lcdLine2 = F("disabled.");
          }
          break;
        case 9:
          lcdLine1=F("PIN:");
          lcdLine2="("+String(lngPIN)+")";
          break;
        case 10:
          lcdLine1=F("Two Minute Warning");
          if (twoMin) {
            lcdLine2 = F("is enabled.");
          } else {
            lcdLine2 = F("is disabled.");
          }
          break;
        case 11:
          lcdLine1=F("Two Minute Song #:");
          lcdLine2=String(twoMinMP3);
          break;
        case 12:
          lcdLine1=F("Alert duration:");
          lcdLine2=String(alertPeriod) + " seconds.";
          break;
        case 13:
          lcdLine1=F("BB Cannon is");
          if (bbCanon) {
            lcdLine2 = F("enabled.");
          } else {
            lcdLine2 = F("disabled.");
          }
          break;
        case 14:
          lcdLine1=F("Juke plays MP3 every");
          lcdLine2=String(songPeriod) + " minutes.";
          break;
        case 15:
          lcdLine1=F("Main Volume is:");
          lcdLine2=String(volMP3) + " (1-30).";
          break;
      }
      writeLCD(0);
    }
    if (key=='#') {
      progSelected = true;
    }
    if (key=='*') {
      progMode = false;
      progSelected = false;
      gameOn = true;
      played = false;
      nextCheck = millis() + 1000;
      clearLCD();
    }
  }
  if (progSelected) {
    // Program option is selected, show initial question and loop back round to get answer. Then set EEPROM and reset program mode.
    byte tmpByte = 0;
    switch (progMenu) {
      case 1:
        if (optionShown) {
          bool waiting = true;
          int i = 0;
          lcdLine3 = "(1 is " + gameNames[i] + ")";
          writeLCD(0);
          menuRefresh = millis();
          while(waiting) {
            key = keypad.getKey();
            if (key != NO_KEY) {
              beep(30);
              switch (key) {
                case '1':
                  gameMode = 1;
                  waiting = false;
                  break;
                case '2':
                  gameMode = 2;
                  waiting = false;
                  break;
                case '3':
                  gameMode = 3;
                  waiting = false;
                  break;
                case '4':
                  gameMode = 4;
                  waiting = false;
                  break;
              }
            }
            if (menuRefresh < millis()) {
              menuRefresh = millis() + 2000;
              lcdLine3 = "(" + String(i+1) + " is " + gameNames[i] + ")";
              writeLCD(0);
              i++;
              if (i>3) i=0;
            }
          }
          lcdLine1 =F("You have selected:");
          lcdLine2 = gameNames[gameMode-1];
          lcdLine3 = F("as the default game.");
          lcdLine4 = "";
          EEPROM.write(0,gameMode);
          writeLCD(1500);
          resetMenu();
        } else {
          lcdLine1 = F("What game? Press 1-4");
          lcdLine2 = F("to select:");
          lcdLine3 = "";
          writeLCD(1500);
          optionShown = true;
        }
        break;
      case 2:
        lcdLine1 = F("Pre-Countdown Locate");
        lcdLine2 = F("Beeping");
        if (preCDBeep) {
          lcdLine3 = F("is now disabled.");
          tmpByte = bitClear(configByte,6);
          configByte = tmpByte;
          preCDBeep = false;
        } else {
          lcdLine3 = F("is now enabled.");
          configByte = configByte |64;
          preCDBeep = true;
        }
        EEPROM.write(1,configByte);
        resetMenu();
        break;
      case 3:
        lcdLine1 = F("Countdown strobe");
        lcdLine3 = "";
        if (cdStrobe) {
          lcdLine2 = F("is now disabled.");
          tmpByte = bitClear(configByte,0);
          configByte = tmpByte;
          mp3_play (29); // Play Disabled
          cdStrobe = false;
        } else {
          lcdLine2 = F("is now enabled.");
          mp3_play (28); // Play Enabled
          configByte = configByte | 1;
          cdStrobe = true;
        }
        EEPROM.write(1,configByte);
        resetMenu();
        break;
      case 4:
        lcdLine1 = F("Countdown beep");
        lcdLine3 = "";
        if (cdBeep) {
          lcdLine2 = F("is now disabled.");
          tmpByte = bitClear(configByte,1);
          mp3_play (29); // Play Disabled
          configByte = tmpByte;
          cdBeep = false;
        } else {
          lcdLine2 = F("is now enabled.");
          configByte = configByte | 2;
          mp3_play (28); // Play Enabled
          cdBeep = true;
        }
        EEPROM.write(1,configByte);
        resetMenu();
        break;
      case 5:
        if (optionShown) {
          cdPeriod = getLong(1,1000,4) * 60;
          clearLCD();
          lcdLine1 = F("Countdown set to");
          lcdLine2 = String(cdPeriod/60) + " minutes.";
          EEPROM.write(3,highByte(cdPeriod));
          EEPROM.write(4,lowByte(cdPeriod));
          resetMenu();
        } else {
          lcdLine1 = F("Countdown length?");
          lcdLine2 = F("(1-1000 minutes)");
          lcdLine3 = "";
          writeLCD(0);
          optionShown = true;
        }
        break;
      case 6:
        lcdLine1 = F("EOD Mode");
        lcdLine3 = "";
        if (eodMode) {
          lcdLine2 = F("is now disabled.");
          mp3_play (29); // Play Disabled
          tmpByte = bitClear(configByte,2);
          configByte = tmpByte;
          eodMode = false;
        } else {
          lcdLine2 = F("is now enabled.");
          mp3_play (28); // Play Enabled
          configByte = configByte | 4;
          eodMode = true;
        }
        EEPROM.write(1,configByte);
        resetMenu();
        break;
      case 7:
        lcdLine1 = F("Canister Sense");
        lcdLine2 = F("");
        if (canSense) {
          lcdLine3 = F("is now disabled.");
          tmpByte = bitClear(configByte,7);
          configByte = tmpByte;
          canSense = false;
        } else {
          lcdLine3 = F("is now enabled.");
          configByte = configByte |128;
          canSense = true;
        }
        EEPROM.write(1,configByte);
        resetMenu();
        break;
      case 8:
        lcdLine1 = F("Card slot sense");
        lcdLine3 = "";
        if (cardSense) {
          lcdLine2 = F("is now disabled.");
          mp3_play (29); // Play Disabled
          tmpByte = bitClear(configByte,4);
          configByte = tmpByte;
          cardSense = false;
        } else {
          lcdLine2 = F("is now enabled.");
          mp3_play (28); // Play Enabled
          configByte = configByte | 16;
          cardSense = true;
        }
        EEPROM.write(1,configByte);
        resetMenu();
        break;
      case 9:
        if (optionShown) {
          lngPIN = getLong(1,999999999,4);
          clearLCD();
          lcdLine1 = "PIN set to:";
          lcdLine2 = String(lngPIN);
          EEPROMWriteLong(5,lngPIN);
          resetMenu();
        } else {
          lcdLine1 = F("Set new PIN code:");
          lcdLine2 = "Current: " + String(lngPIN);
          lcdLine3 = F("Range 1 to 999999999");
          writeLCD(0);
          optionShown = true;
        }
        break;
      case 10:
        lcdLine1 = F("Two Minute Warning");
        lcdLine3 = "";
        if (twoMin) {
          lcdLine2 = F("is now disabled.");
          mp3_play (29); // Play Disabled
          tmpByte = bitClear(configByte,5);
          configByte = tmpByte;
          twoMin = false;
        } else {
          lcdLine2 = F("is now enabled.");
          mp3_play (28); // Play Enabled
          configByte = configByte | 32;
          twoMin = true;
        }
        EEPROM.write(1,configByte);
        resetMenu();
        break;
      case 11:
        if (optionShown) {
          twoMinMP3 = getLong(0,mp3Top,4);
          clearLCD();
          lcdLine1 = F("Two Minute song");
          lcdLine2 = "track # is " + String(twoMinMP3) + "";
          EEPROM.write(11,twoMinMP3);
          resetMenu();
        } else {
          lcdLine1 = F("Which Two Minute");
          lcdLine2 = F("song track? 0-") + String(mp3Top);
          lcdLine3 = "";
          writeLCD(0);
          optionShown = true;
        }
        break;
      case 12:
        if (optionShown) {
          alertPeriod = getLong(1,240,4);
          clearLCD();
          lcdLine1 = F("Alert Duration set");
          lcdLine2 = "to " + String(alertPeriod) + " seconds.";
          EEPROM.write(2,alertPeriod);
          resetMenu();
        } else {
          lcdLine1 = F("Duration of alert?");
          lcdLine2 = F("(1-240 seconds)");
          lcdLine3 = "";
          writeLCD(0);
          optionShown = true;
        }
        break;
      case 13:
        lcdLine1 = F("BB Cannon");
        lcdLine3 = "";
        if (bbCanon) {
          lcdLine2 = F("is now disabled.");
          mp3_play (29); // Play Disabled
          tmpByte = bitClear(configByte,3);
          configByte = tmpByte;
          bbCanon = false;
        } else {
          lcdLine2 = F("is now enabled.");
          mp3_play (28); // Play Enabled
          configByte = configByte | 8;
          bbCanon = true;
        }
        EEPROM.write(1,configByte);
        resetMenu();
        break;
      case 14:
        if (optionShown) {
          songPeriod = getLong(0,60,4);
          clearLCD();
          lcdLine1 = F("Minutes between MP3");
          lcdLine2 = "random songs: " + String(songPeriod);
          EEPROM.write(9,songPeriod);
          resetMenu();
        } else {
          lcdLine1 = F("Minutes between MP3");
          lcdLine2 = F("random songs? 0-60");
          lcdLine3 = "";
          writeLCD(0);
          optionShown = true;
        }
        break;
      case 15:
        if (optionShown) {
          volMP3 = getLong(1,30,4);
          clearLCD();
          lcdLine1 = F("Main volume set");
          lcdLine2 = "to " + String(volMP3) + ".";
          EEPROM.write(10,volMP3);
          mp3_set_volume (volMP3);
          resetMenu();
        } else {
          lcdLine1 = F("Main MP3 volume?");
          lcdLine2 = F("(1-30)");
          lcdLine3 = "";
          writeLCD(0);
          optionShown = true;
        }
        break;
    }
  }
}

void beep(int period) {
  digitalWrite(buzzPIN,HIGH);
  delay(period);
  digitalWrite(buzzPIN,LOW);
}

void resetMenu() {
  writeLCD(2500);
  optionShown = false;
  progSelected = false;
  firstRun = true;
  progMenu = 0;
  key = keypad.getKey();
}

void altStrobe() {
  if (cdStrobe) {
    if (blnWhite) {
      digitalWrite(ledBluePIN,HIGH);
      blnWhite = false;
    } else {
      digitalWrite(ledWhitePIN,HIGH);
      blnWhite = true;
    }
  }
  if (cdBeep) {
    beep(30);
  } else {
    delay(30);
  }
  digitalWrite(ledWhitePIN,LOW);
  digitalWrite(ledBluePIN,LOW);
}

void writeLCD(int pause) {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(lcdLine1);
  lcd.setCursor(0,1);
  lcd.print(lcdLine2);
  lcd.setCursor(0,2);
  lcd.print(lcdLine3);
  lcd.setCursor(0,3);
  lcd.print(lcdLine4);
  delay(pause);
}

void clearLCD() {
  lcd.clear();
  lcdLine1="";
  lcdLine2="";
  lcdLine3="";
  lcdLine4="";
}

long getLong(int minV, long maxV, byte displayLine) {
  // Get int min to max from keypad and return Long
  bool finished = false;
  String lcdLine = "                    ";
  long num = 0;
  unsigned long nextBeep = millis() + 5000;
  key = keypad.getKey();
  while(!finished)
  {
    if ((preCDBeep) && (nextBeep < millis())) {
      nextBeep = millis() + 5000;
      beep(300);
    }
    switch (key) {
       case NO_KEY:
          break;
       case '0': case '1': case '2': case '3': case '4':
       case '5': case '6': case '7': case '8': case '9':
          beep(30);
          num = num * 10 + (key - '0');
          lcdLine = String(num);
          break;
       case '*':
          beep(30);
          num = 0;
          lcdLine = "";
          break;
       case '#':
          if (num >= minV && num <= maxV) {
            beep(30);
            finished = true;  
          } else {
            num = 0;
            lcdLine = F("Invalid Entry!");
            beep(1000);
            lcdLine = "";
            finished = false;
          }
          break;          
    }
    if (key != NO_KEY) {
      switch (displayLine) {
        case 4:
          lcdLine4 = lcdLine;
        break;
        case 3:
          lcdLine3 = lcdLine;
        break;
        case 2:
          lcdLine2 = lcdLine;
        break;
        default:
          lcdLine1 = lcdLine;
        break;
      }
      writeLCD(0);
    }
    key = keypad.getKey();
  }
  return num;
}

void EEPROMWriteLong(int address, long value) {
    byte four = (value & 0xFF);
    byte three = ((value >> 8) & 0xFF);
    byte two = ((value >> 16) & 0xFF);
    byte one = ((value >> 24) & 0xFF);
    EEPROM.write(address, four);
    EEPROM.write(address + 1, three);
    EEPROM.write(address + 2, two);
    EEPROM.write(address + 3, one);
}

long EEPROMReadLong(long address) {
    long four = EEPROM.read(address);
    long three = EEPROM.read(address + 1);
    long two = EEPROM.read(address + 2);
    long one = EEPROM.read(address + 3);
    return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}


