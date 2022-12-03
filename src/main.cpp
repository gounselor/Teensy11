#include <Arduino.h>
#include <stdint.h>
#include <SPI.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
//#include <Adafruit_SSD1306.h>

#if defined(ARDUINO_TEENSY40)
#define SD_PIN 10 // Teensy
#define USE_SHARED_SPI 1
#include <SdFat.h>
#elif defined(ARDUINO_TEENSY41)
#include "SdFat.h"
#include "sdios.h"
#define SD_FAT_TYPE 3
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI)
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI)
#endif  // HAS_SDIO_CLASS
#endif

#if defined(ARDUINO_TEENSY41)

const uint8_t SD_CS_PIN = SS;
SdFs sd;
FsFile file;
FsFile root;
#else 
SdFat sd;
#endif

#include <CLI.h>

#include "TeensyTimerTool.h"

#include "pdp11.h"
#include "rk05.h"
#include "tm11.h"
#include "dl11.h"
#include "unibus.h"
#include "cpu.h"
#include "console.h"

using namespace TeensyTimerTool;

extern "C"
{
    struct usb_string_descriptor_struct
    {
        uint8_t bLength;
        uint8_t bDescriptorType;
        uint16_t wString[10];
    };

    usb_string_descriptor_struct usb_string_serial_number =
    {
         16,  // 2 + 2*length of the sn string
         3,
         {'P', 'D', 'P', '1', '1', '4', '0', 0, 0, 0},
    };
}

Adafruit_8x16matrix m70 = Adafruit_8x16matrix();
Adafruit_8x16matrix m71 = Adafruit_8x16matrix();
Adafruit_AlphaNum4  m72 = Adafruit_AlphaNum4();
Adafruit_AlphaNum4  m73 = Adafruit_AlphaNum4();

//Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);

//Timer lks;
PeriodicTimer lks;
RTC_DS3231 rtc;

bool boot = false;
int  trace = 0;

uint16_t tcounter = 0;
uint16_t hz = 60;
uint32_t scounter = 0;
uint32_t ips = 0;

static void loop0();
  
jmp_buf trapbuf;

void toggle_trace() {
  trace = 1000;
}

/*
  (c) Frank B, 2020
  License: MIT
  Please keep this info.
*/

FLASHMEM
void flexRamInfo(void) {

#if defined(ARDUINO_TEENSY40)
  //static const unsigned DTCM_START = 0x20000000UL;
  static const unsigned OCRAM_START = 0x20200000UL;
  static const unsigned OCRAM_SIZE = 512;
  static const unsigned FLASH_SIZE = 1984;
#elif defined(ARDUINO_TEENSY41)
  //static const unsigned DTCM_START = 0x20000000UL;
  static const unsigned OCRAM_START = 0x20200000UL;
  static const unsigned OCRAM_SIZE = 512;
  static const unsigned FLASH_SIZE = 7936;
#endif

  int itcm = 0;
  int dtcm = 0;
  int ocram = 0;
  uint32_t gpr17 = IOMUXC_GPR_GPR17;

  char __attribute__((unused)) dispstr[17] = {0};
  dispstr[16] = 0;

  for (int i = 15; i >= 0; i--) {
    switch ((gpr17 >> (i * 2)) & 0b11) {
      default: dispstr[15 - i] = '.'; break;
      case 0b01: dispstr[15 - i] = 'O'; ocram++; break;
      case 0b10: dispstr[15 - i] = 'D'; dtcm++; break;
      case 0b11: dispstr[15 - i] = 'I'; itcm++; break;
    }
  }

  Serial.printf("ITCM : %dkB, DTCM: %dkB, OCRAM: %d(+%d)kB [%s]\r\n", 
                itcm * 32, dtcm * 32, ocram * 32, OCRAM_SIZE, dispstr);
  const char* fmtstr = "%-6s%7d %5.02f%% of %4dkB (%7d Bytes free) %s\r\n";

  extern unsigned long _stext;
  extern unsigned long _etext;
  //extern unsigned long _sdata;
  //extern unsigned long _ebss;
  extern unsigned long _flashimagelen;
  extern unsigned long _heap_start;
  //extern unsigned long _estack;

  Serial.printf(fmtstr, "ITCM :",
                (unsigned)&_etext - (unsigned)&_stext,
                (float)((unsigned)&_etext - (unsigned)&_stext) / ((float)itcm * 32768.0f) * 100.0f,
                itcm * 32,
                itcm * 32768 - ((unsigned)&_etext - (unsigned)&_stext), "(RAM1) FASTRUN");

  Serial.printf(fmtstr, "OCRAM:",
                (unsigned)&_heap_start - OCRAM_START,
                (float)((unsigned)&_heap_start - OCRAM_START) / (OCRAM_SIZE * 1024.0f) * 100.0f,
                OCRAM_SIZE,
                OCRAM_SIZE * 1024 - ((unsigned)&_heap_start - OCRAM_START), "(RAM2) DMAMEM, Heap");

  Serial.printf(fmtstr, "FLASH:",
                (unsigned)&_flashimagelen,
                ((unsigned)&_flashimagelen) / (FLASH_SIZE * 1024.0f) * 100.0f,
                FLASH_SIZE,
                FLASH_SIZE * 1024 - ((unsigned)&_flashimagelen), "FLASHMEM, PROGMEM");
}

void displayWord(Adafruit_AlphaNum4 *hi, Adafruit_AlphaNum4 *lo, uint32_t val) {
  //hi->clear();
  //lo->clear();
  for (int i = 0; i < 8; i++) {
    char digit = val & 7;
    switch (i) {
      case 0:
        lo->writeDigitAscii(3, digit + 0x30);
        break;
      case 1:
        lo->writeDigitAscii(2, digit + 0x30);
        break;
      case 2:
        lo->writeDigitAscii(1, digit + 0x30);
        break;
      case 3:
        lo->writeDigitAscii(0, digit + 0x30);
        break;
      case 4:
        hi->writeDigitAscii(3, digit + 0x30);
        break;
      case 5:
        hi->writeDigitAscii(2, digit + 0x30);
        break;
      case 6:
        hi->writeDigitAscii(1, digit + 0x30);
        break;
      case 7:
        hi->writeDigitAscii(0, digit + 0x30);
        break;
    }
    val = val >> 3;    
  }
  hi->writeDisplay();
  lo->writeDisplay();
}

void displayWordExternal(uint32_t val) {
  displayWord(&m72, &m73, val);
}

void writeByte(Adafruit_8x16matrix *mx, uint8_t line, uint8_t val) {
  mx->drawPixel(line, 0, val & (1 << 0));
  mx->drawPixel(line, 1, val & (1 << 1));
  mx->drawPixel(line, 2, val & (1 << 2));
  mx->drawPixel(line, 3, val & (1 << 3));
  mx->drawPixel(line, 4, val & (1 << 4));
  mx->drawPixel(line, 5, val & (1 << 5));
  mx->drawPixel(line, 6, val & (1 << 6));
  mx->drawPixel(line, 7, val & (1 << 7));
}

void writeWord(Adafruit_8x16matrix *mx, uint8_t line, uint16_t val) {
  writeByte(mx, line, val & 0xFF);
  writeByte(mx, line + 8, val >> 8);
}

void displayRegisters() {
  writeWord(&m70, 0, cpu::R[0]);
  writeWord(&m70, 1, cpu::R[2]);
  writeWord(&m70, 2, cpu::R[4]);
  writeWord(&m70, 3, cpu::R[6]);
  writeWord(&m70, 7, cpu::PS.Word);

  writeWord(&m71, 0, cpu::R[1]);
  writeWord(&m71, 1, cpu::R[3]);
  writeWord(&m71, 2, cpu::R[5]);
  writeWord(&m71, 3, cpu::R[7]);
  writeWord(&m71, 7, tcounter);
  //writeWord(&m71, 7, (rk11::cylinder << 8) | (rk11::sector << 4) | (1 << rk11::drive));
  displayWord(&m72, &m73, (rk11::cylinder << 12) | (rk11::sector));
  m70.writeDisplay();
  m71.writeDisplay();
  yield();
}


void lks_tick() {
  //yield();
  tcounter++;
  if (0 == --hz) {
    hz = 60;
    ips = scounter;
    scounter = 0;
  }
  if (!console::active) {
    displayRegisters();
    //uint16_t csw = unibus::SWR;
    //displayWord(&m72, &m73, csw);
    cpu::LKS |= (1 << 7);
    if (cpu::LKS & (1 << 6)) {
      cpu::interrupt(INTCLOCK, 6);
    }
  }
}

void panic() {
  __disable_irq();
  print_state();
  displayRegisters();
  __enable_irq();
  console::loop(true);
  //abort();
}

void setup() {
  String cmd;
  Serial.begin(115200);
  Serial.setTimeout(0);
  while(!Serial);
  Serial.println();
#if defined(ARDUINO_TEENSY40)
  pinMode(SD_PIN, OUTPUT);
  if (!sd.begin(SD_PIN, SPI_FULL_SPEED)) {
    sd.initErrorHalt();
  }
#else
  if (!sd.begin(SD_CONFIG)) {
    sd.initErrorHalt();
  }
#endif

  Wire.begin();
  Serial.printf("I2C bus addr: ");
  for (byte i = 0; i < 127; i++) { 
    Wire.beginTransmission (i);
    if (Wire.endTransmission () == 0) {      
      Serial.printf("0x%02x ", i);
      delay(10);
    } 
  }
  Serial.println(); 
  Serial.printf("CPU  : %d MHz\r\n", F_CPU_ACTUAL / 1000 / 1000);
  extern uint8_t external_psram_size;
  Serial.printf("PSRAM: %dMb\r\n", external_psram_size);

  /* Lib is broken
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    display.setRotation(0); //default
    display.setTextSize(2); 
    display.setTextColor(SSD1306_WHITE); 
    display.display();
    delay(1000);
    display.clearDisplay();
    display.println("PDP11/40");
    display.setTextSize(1);
    display.println("Initializing...");
    display.display();
    haveSSD1306 = true;
  }
  */

  m70.begin(0x70);  // pass in the address
  m71.begin(0x71);
  m72.begin(0x72);
  m73.begin(0x73);
  m70.setRotation(3);
  m71.setRotation(3);
  for (int i = 0; i < 8; i++) {  
    writeWord(&m70, i, 0xFFFF);
    writeWord(&m71, i, 0xFFFF);
    m72.writeDigitAscii(0, 0x30 + i);
    m72.writeDigitAscii(1, 0x30 + i);
    m72.writeDigitAscii(2, 0x30 + i);
    m72.writeDigitAscii(3, 0x30 + i);
    m73.writeDigitAscii(0, 0x30 + i);
    m73.writeDigitAscii(1, 0x30 + i);
    m73.writeDigitAscii(2, 0x30 + i);
    m73.writeDigitAscii(3, 0x30 + i);
    m70.writeDisplay();
    m71.writeDisplay();
    m72.writeDisplay();
    m73.writeDisplay();
    delay(100);
  }
  m70.clear();
  m71.clear();
  m72.clear();
  m73.clear();
  m70.writeDisplay();
  m71.writeDisplay();
  m72.writeDisplay();
  m73.writeDisplay();
  /*
  if (haveSSD1306) {
    display.clearDisplay();
    display.display();
  }
  */
  m72.writeDigitAscii(0, 'P');
  m72.writeDigitAscii(1, 'D');
  m72.writeDigitAscii(2, 'P');
  m72.writeDigitAscii(3, '1');
  m73.writeDigitAscii(0, '1');
  m73.writeDigitAscii(1, '/');
  m73.writeDigitAscii(2, '4');
  m73.writeDigitAscii(3, '0');
  m72.writeDisplay();
  m73.writeDisplay();

  flexRamInfo();
  // must be before cpu::reset and console::loop to prevent clearing of the boot rom or deposit code
  unibus::reset(); 
  console::loop(false);
  cpu::reset();
  //lks.beginPeriodic(lks_tick, 16667);
  lks.begin(lks_tick, 16667);
}

void loop() {  
  yield();
  uint16_t vec = setjmp(trapbuf);
  if (vec) {
    cpu::trapat(vec);
  }
  loop0();  
}

static void loop0() {
  for (;;) {  
    cpu::step();
    scounter++;
    yield();    // without yield, strange things happen
    /* XXX STKL
    if (cpu::TRAP_REQ) {
      longjmp(trapbuf, cpu::TRAP_REQ);
    }
    */        
    __disable_irq();
    //the itab check is very cheap
    if ((itab[0].vec) && (itab[0].pri >= ((cpu::PS.Word >> 5) & 7))) {
      __enable_irq();
      cpu::handleinterrupt();
      return; // exit from loop to reset trapbuf
    }    
    __enable_irq();
    // costs 3 usec
    dl11::poll();
  }
}
