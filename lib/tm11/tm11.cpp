#include <Arduino.h>

#include <pdp11.h>
#include <unibus.h>
#include "tm11.h"
#include "cpu.h"

#include "SdFat.h"

#define DEBUG_TM11 0

#define TM_GO    01
#define TM_RCOM  02
#define TM_WCOM  04
#define TM_WEOF  06
#define TM_SFORW 010
#define TM_SREV  012
#define TM_WIRG  014
#define TM_REW   016

#define TM_CRDY  0200
#define TM_IE    0100
#define TM_CE    0100000

#define TM_TUR   01
#define TM_WRL   04
#define TM_BOT   040
#define TM_SELR  0100
#define TM_NXM   0200
#define TM_EOT   02000
#define TM_EOF   040000
#define TM_ILC   0100000

#define TAPE_LEN 6195200

#define TAPE_EOF 0x00000000
#define TAPE_EOT 0xFFFFFFFF

namespace tm11 {

    uint16_t MTS;   // 772520 Status Register	
    uint16_t MTC;   // 772522 Command Register		
    uint16_t MTBRC; // 772524 Byte Record Counter
    uint16_t MTCMA; // 772526 Current Memory Address Register
    uint16_t MTD;   // 772530 Data Buffer
    uint16_t MTRD;  // 772532 TU10 Read Lines

    uint16_t rewind;

    uint16_t sector, address, count;

    struct tape tmdata[TM_NUM_DRV];

    void reset() {
        MTS = TM_TUR;
        MTC = TM_CRDY;
        for (int i = 0; i < 8; i++) {
            tmdata[i].pos = 0;
        }
        MTBRC = 0;
        MTCMA = 0;        
        MTRD = 0;
        MTD = 0;
    }

    uint16_t read16(uint32_t a) {
        /*
        if (DEBUG_TM11) {
            Serial.printf("tm11: read16 : %06o\r\n", a);
        }
        */
        switch(a & ~1) {
            case 0772520:
                return MTS;
            case 0772522:
                MTC &= 0x7FFF;
                if (MTS & 0xFF80) MTC |= 0x8000;
                return MTC;
            case 0772524:
                return MTBRC;
            case 0772526:
                return MTCMA;   
            case 0772530:
                return MTD;
            case 0772532:
                return MTRD; 
            default:
                Serial.printf("tm11: invalid read16 %06o\r\n", a);
                return 0;
        }                
    }

    void write16(uint32_t a, uint16_t v) {     
        
        if (DEBUG_TM11) {
            Serial.printf("tm11: write16: %06o: %06o\r\n", a, v);
        }
        
        switch(a) {
            case 0772522:
                MTC &= (TM_CRDY|TM_CE);
                MTC |= v & ~(TM_CRDY|TM_CE);
                if (MTC & (TM_GO|TM_IE)) {
                    MTC &= ~TM_CRDY;
                    go();
                }
                break;
            case 0772524:
                MTBRC = v;
                break;
            case 0772526:
                MTCMA = v;  
                break;
            case 0772530:
                MTD = v;
                break;
            case 0772532:
                break;
            default:
                Serial.printf("tm11: invalid write16 %06o: %06o\r\n", a, v);
        }
    }

    uint16_t cmd_end() {
        if (rewind) {
           rewind--;
           if (!rewind) {                
                MTS &= ~2;   // clear rewind
                MTS |= 0x21; // set bot and tur
                MTC |= 0x80; // set cur
                Serial.printf("tm11: rewind done, MTS: %06o, MTC: %06o\r\n", MTS, MTC);
           }
        }

        MTS |= 1;
        MTC |= 0x80;
        return MTC & 0x40;
    }

    void finish() {
        if (MTS & (TM_ILC|TM_EOT|TM_NXM))
            MTC |= TM_CE;
        MTC |= TM_CRDY;
        if (MTC & TM_IE) {
            MTC &= ~TM_IE;
            cpu::interrupt(INTTM, 5);
        }
        if (DEBUG_TM11) {
            Serial.printf("tm11: MTS: %06o, MTC: %06o, MTBRC: %06o, MTCMA: %06o, fin\r\n", MTS, MTC, MTBRC, MTCMA);
        }
        cmd_end();
    }

    void go() {
        uint8_t drive = (MTC >> 8) & 3;
        bool attached = tmdata[drive].attached;
        off_t pos = tmdata[drive].pos;
        if (!attached) {
            Serial.printf("tm11: drive %d is not attached\r\n", drive);
            longjmp(trapbuf, INTBUS);
            return;
        }

        MTC &= ~TM_CE;
        MTS &= ~(TM_ILC|TM_NXM);
        
        uint8_t cmd = (MTC >> 1) & 7;
        if (DEBUG_TM11) {
            Serial.printf("tm11: MTS: %06o, MTC: %06o, MTBRC: %06o, MTCMA: %06o, cmd: %d, pos: %d\r\n", MTS, MTC, MTBRC, MTCMA, cmd, pos);
        }
        switch (cmd) {
            case 0: { // off-line
                // clr tur? and cur?
                break;
            }
            case 1: { // read           
                MTS &= ~TM_EOF;
                uint32_t addr = (MTC & 060) << 12 | MTCMA;
                
                if (DEBUG_TM11) {
                    Serial.printf("tm11: read: pos %d, addr: %06o, mtbrc: %d/%06o\r\n", tmdata[drive].pos, addr, MTBRC, MTBRC);
                }
                if (tmdata[drive].pos > (off_t) tmdata[drive].file.fileSize()) {
                    MTS |= TM_EOT;
                    break;
                }
                __disable_irq();
                tmdata[drive].file.seekSet(tmdata[drive].pos);
                while(MTBRC) {
                    MTS &= ~TM_BOT;
                    uint16_t dv = tmdata[drive].file.read(); 
                    MTBRC++;
                    dv |= (tmdata[drive].file.read() << 8);    
                    MTBRC++;
                    
                    if (DEBUG_TM11) {
                        Serial.printf("tm11: read %06o -> %08o\r\n", dv, addr);
                        yield();
                    }
                    
                    unibus::write16(addr, dv);
                    addr+=2;
                    MTCMA = addr & 0xFFFF;
                    MTC  |= ((addr & 0x300000000) >> 12);
                    tmdata[drive].pos += 2;
                    if (tmdata[drive].pos > (off_t) tmdata[drive].file.fileSize()) {
                        MTS |= TM_EOT;
                        break;
                    }
                }    
                __enable_irq();
                yield();
                break;
            }
            case 2: { // write
                MTC &= ~TM_EOF;
                uint32_t addr = (MTC & 060) << 12 | MTCMA;
                if (DEBUG_TM11) {
                    Serial.printf("tm11: write: pos %d, addr: %06o, mtbrc: %d/%06o\r\n", tmdata[drive].pos, addr, MTBRC, MTBRC);
                }
                if (tmdata[drive].pos >= TAPE_LEN) {
                    MTS |= TM_EOT;
                    break;
                }
                __disable_irq();
                tmdata[drive].file.seekSet(tmdata[drive].pos);
                while(MTBRC) {
                    MTS &= ~TM_BOT;
                    if (DEBUG_TM11) {
                        Serial.printf("tm11: read %08o\r\n", addr);
                        yield();
                    }
                    uint16_t dv = unibus::read16(addr);
                    tmdata[drive].file.write(dv & 0xFF);
                    MTBRC++;
                    tmdata[drive].file.write((dv >> 8) & 0xFF);
                    MTBRC++;
                    addr+=2;
                    MTCMA = addr & 0xFFFF;
                    MTC |= ((addr & 0x300000000) >> 12);
                    tmdata[drive].pos += 2;
                }
                MTC |= TM_EOF;
                __enable_irq()
                yield();
                break;
            }
            case 3: { // write eof 
                MTC &= ~TM_EOF;
                break;
            }
            case 4: { // space forward
                Serial.println("tm11: space forward uninplemented");
                break;
            }
            case 5: { // space reverse
                Serial.println("tm11: space reverse unimplemented");
                break;
            }
            case 6: { // write with extended IRG
                Serial.println("tm11: write with extended irg uninplemented");
                break;
            }
            case 7: { // rewind
                Serial.println("tm11: rewind");                
                MTS &= ~(TM_EOT|TM_EOF);
                if (MTC & 0x40) {
                    cpu::interrupt(INTTM, 5);
                }                
                if (!tmdata[drive].file.seekSet(0)) {
                    Serial.printf("tm11: failed to seek: drive: %d, pos: %d\r\n", drive, pos);
                }                
                tmdata[drive].file.flush();
                tmdata[drive].pos = 0;
                rewind = 1;                
                break;
            }
            default: {
                Serial.printf("tm11: cmd %d uninplemented\r\n", cmd);
                if (MTC & TM_GO) {
                    MTS |= TM_ILC;
                }
                break;
            }
        }
        finish();
    }        
};
