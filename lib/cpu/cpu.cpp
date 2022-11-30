#include <SdFat.h>
#include <pdp11.h>
#include <mutex.h>
#include "mmu.h"
#include "dl11.h"
#include "unibus.h"
#include "cpu.h"

#include "bootrom.h"
#include "rk05.h"
#include "tm11.h"

#define GET_SIGN_W(v)   (((v) >> 15) & 1)
#define GET_SIGN_B(v)   (((v) >> 7) & 1)

pdp11::intr itab[ITABN];

extern int trace;

namespace cpu {

bool debug = false;

#ifdef INVLOG
FsFile invlog;
#endif

#define STKL_R          0340                            /* stack limit */
#define STKL_Y          0400

// registers
uint32_t R[8];
/*
  15  14  13  12  11  10  09  08  07  06  05  04  03  02  01  00
+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
| C | C | P | P |   |   |   |   | S | P | L | T | N | Z | V | C |
+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

PSW PS; // processor status 777776
uint32_t PC; // address of current instruction
uint32_t lastPC;
uint32_t KSP, USP; // kernel and user stack pointer
uint32_t LKS;      // clock1
//uint16_t TRAP_REQ;

bool curuser, prevuser, g_cmd = false;

void reset(void) {
  if (!g_cmd) {
    LKS = 1 << 7;
    for (uint32_t i = 0; i < 29; i++) {
      unibus::write16(02000 + (i * 2), bootrom[i]);
    }
    R[7] = 02002;
  }
  // trap catcher
  unibus::write16(000000, 000777);
  unibus::write16(000002, 000000);
  unibus::write16(000004, 000006);
  unibus::write16(000006, 000000);
  unibus::write16(000010, 000012);
  unibus::write16(000012, 000000); 
  unibus::write16(000024, 000026); 
  unibus::write16(000024, 000000); 
  mmu::reset();
  dl11::reset();
  rk11::reset();
  tm11::reset();
#ifdef INVLOG
  invlog.close();
  invlog.open("/invalid.log", O_CREAT|O_APPEND|O_WRITE);
#endif
}

static uint16_t read8(const uint32_t a) {
  return unibus::read8(mmu::decode(a, false, curuser));
}

static uint16_t read16(const uint32_t a) {
  return unibus::read16(mmu::decode(a, false, curuser));
}

static void write8(const uint32_t a, const uint32_t v) {
  unibus::write8(mmu::decode(a, true, curuser), v);
}

static void write16(const uint32_t a, const uint32_t v) {
  unibus::write16(mmu::decode(a, true, curuser), v);
}

static inline bool isReg(const uint32_t a) {
  return (a & 0177770) == 0170000;
}

static uint16_t memread16(const uint32_t a) {
  if (isReg(a)) {
    return R[a & 7];
  }
  return read16(a);
}

static uint16_t memread(const uint32_t a, const uint32_t l) {
  if (debug) {
    Serial.printf("DEBUG: memread: PC: %06o, %06o: %03o\r\n", PC, a, l);
  }
  if (isReg(a)) {
    if (debug) {
      Serial.printf("DEBUG: memread: PC: %06o, register: %03o\r\n", PC, a & 7);
    }
    const uint32_t r = a & 7;
    if (l == 2) {
      return R[r];
    } else {
      return R[r] & 0xFF;
    }
  }
  if (l == 2) {
    return read16(a);
  }
  return read8(a);
}

static void memwrite16(const uint32_t a, const uint32_t v) {
  if (isReg(a)) {
    R[a & 7] = v;
    /* XXX STKL
    if ((a & 7) == 6 && !curuser) { // kernel mode
      if (v < unibus::SLR + STKL_Y) {
        TRAP_REQ = 4;
      }
    }
    */
  } else {
    write16(a, v);
  }
}

static void memwrite(const uint32_t a, const uint32_t l, const uint32_t v) {
  if (isReg(a)) {
    const uint8_t r = a & 7;
    if (l == 2) {
      R[r] = v;
    } else {
      R[r] &= 0xFF00;
      R[r] |= v;
    }
    return;
  }
  if (l == 2) {
    write16(a, v);
  } else {
    write8(a, v);
  }
}

static uint16_t fetch16() {
  const uint32_t val = read16(R[7]);
  R[7] += 2;
  return val;
}

static void push(const uint32_t v) {
  R[6] -= 2;
  write16(R[6], v);
}

static uint16_t pop() {
  const uint32_t val = read16(R[6]);
  R[6] += 2;
  return val;
}

// aget resolves the operand to a vaddress.
// if the operand is a register, an address in
// the range [0170000,0170007). This address range is
// technically a valid IO page, but unibus doesn't map
// any addresses here, so we can safely do this.
static uint16_t aget(uint32_t v, uint32_t l) {
  if (debug) {
    Serial.printf("DEBUG: aget: PC: %06o, %06o, %03o\r\n", PC, v, l);
  }
  if ((v & 070) == 000) {     // register mode    
    return 0170000 | (v & 7); // return pseudo register addr
  }
  uint32_t addr = 0;
  switch (v & 060) {  // which mode?
    case 000:         // mode 0 register
      v &= 7;
      addr = R[v & 7];
      // aget: case 000: v: 001, addr: 177412
      // 010311 MOV R3, (R1)
      break;
    case 020:         // mode 2 autoincrement
      if (((v & 7) >= 6) || ((v & 010) != 0)) {
        l = 2;
      }
      addr = R[v & 7];
      R[v & 7] += l;
      break;
    case 040:        // mode 4 autodecrement
      if (((v & 7) >= 6) || ((v & 010) != 0)) {
        l = 2;
      }
      R[v & 7] -= l;
      addr = R[v & 7];
      break;
    case 060:       //  mode 6 index
      addr = fetch16();
      addr += R[v & 7];
      break;
  }
  // addr &= 0xFFFF;
  if (v & 010) { // handle deferred
    addr = read16(addr);
  }
  return addr;
}

static void branch(uint32_t o) {
  if ((o & 0x80) == 0x80) {
    o = -(((~o) + 1) & 0xFF);
  }
  o <<= 1;
  R[7] += o;
}

void switchmode(const bool newm) {
  prevuser = curuser;
  curuser = newm;
  if (prevuser) {
    USP = R[6];
  } else {
    KSP = R[6];
  }
  if (curuser) {
    R[6] = USP;
  } else {
    R[6] = KSP;
  }
  PS.Word &= 0007777;
  if (curuser) {
    PS.Word |= (1 << 15) | (1 << 14);
  }
  if (prevuser) {
    PS.Word |= (1 << 13) | (1 << 12);
  }
}

static void MOV(const uint32_t instr) {
  //istat[0]++;
  const uint32_t d = instr & 077;
  const uint32_t s = (instr & 07700) >> 6;
  uint32_t l = 2 - (instr >> 15);
  const uint32_t msb = l == 2 ? 0x8000 : 0x80;
  uint32_t uval = memread(aget(s, l), l);
  const uint32_t da = aget(d, l);
  PS.Word &= 0xFFF1;
  if (uval & msb) {
    PS.Flags.N = 1;
  }
  if (uval == 0) PS.Flags.Z = 1;
  if ((isReg(da)) && (l == 1)) {
    l = 2;
    if (uval & msb) {
      uval |= 0xFF00;
    }
  }
  //Serial.printf("mov %06o, %06o\r\n", da, uval);
  memwrite(da, l, uval);
}

static void CMP(uint32_t instr) {
  //istat[1]++;
  const uint32_t d = instr & 077;
  const uint32_t s = (instr & 07700) >> 6;
  const uint32_t l = 2 - (instr >> 15);
  const uint32_t msb = l == 2 ? 0x8000 : 0x80;
  const uint32_t max = l == 2 ? 0xFFFF : 0xFF;
  uint32_t val1 = memread(aget(s, l), l);
  const uint32_t da = aget(d, l);
  uint32_t val2 = memread(da, l);
  const uint32_t sval = (val1 - val2) & max;
  PS.Word &= 0xFFF0;
  if(sval == 0) PS.Flags.Z = 1;
  if (sval & msb) {
    PS.Flags.N = 1;
  }
  if (((val1 ^ val2) & msb) && (!((val2 ^ sval) & msb))) {
    PS.Flags.V = 1;
  }
  if (val1 < val2) {
    PS.Flags.C = 1;
  }
}

static void BIT(uint32_t instr) {
  //istat[2]++;
  const uint32_t d = instr & 077;
  const uint32_t s = (instr & 07700) >> 6;
  const uint32_t l = 2 - (instr >> 15);
  const uint32_t msb = l == 2 ? 0x8000 : 0x80;
  const uint32_t val1 = memread(aget(s, l), l);
  const uint32_t da = aget(d, l);
  const uint32_t val2 = memread(da, l);
  const uint32_t uval = val1 & val2;
  PS.Word &= 0xFFF1;
  if (uval == 0) PS.Flags.Z =1;
  if (uval & msb) {
    PS.Flags.N = 1;
  }
}

static void BIC(uint32_t instr) {
  //istat[3]++;
  const uint32_t d = instr & 077;
  const uint32_t s = (instr & 07700) >> 6;
  const uint32_t l = 2 - (instr >> 15);
  const uint32_t msb = l == 2 ? 0x8000 : 0x80;
  const uint32_t max = l == 2 ? 0xFFFF : 0xFF;
  const uint32_t val1 = memread(aget(s, l), l);
  const uint32_t da = aget(d, l);
  const uint32_t val2 = memread(da, l);
  const uint32_t uval = (max ^ val1) & val2;
  PS.Word &= 0xFFF1;
  if (uval == 0) PS.Flags.Z = 1;
  if (uval & msb) {
    PS.Flags.N = 1;
  }
  memwrite(da, l, uval);
}

static void BIS(uint32_t instr) {
  //istat[4]++;
  uint32_t d = instr & 077;
  uint32_t s = (instr & 07700) >> 6;
  uint32_t l = 2 - (instr >> 15);
  uint32_t msb = l == 2 ? 0x8000 : 0x80;
  uint32_t val1 = memread(aget(s, l), l);
  uint32_t da = aget(d, l);
  uint32_t val2 = memread(da, l);
  uint32_t uval = val1 | val2;
  PS.Word  &= 0xFFF1;
  if (uval == 0) PS.Flags.Z = 1;
  if (uval & msb) {
    PS.Flags.N = 1;
  }
  memwrite(da, l, uval);
}
/*
DEBUG: aget: PC: 011504, 000027, 002
DEBUG: aget: PC: 011504, 000067, 002
DEBUG: PC: 011504, ADD: da: 017360, val1: 177777, val2: 000000, uval: 177777
*/
static void ADD(uint32_t instr) {
  //istat[5]++;
  uint32_t d = instr & 077;
  uint32_t s = (instr & 07700) >> 6;
  //uint8_t l = 2 - (instr >> 15);
  uint32_t val1 = memread16(aget(s, 2));
  uint32_t da = aget(d, 2);
  uint32_t val2 = memread16(da);
  uint32_t uval = (val1 + val2) & 0xFFFF;
  PS.Word &= 0xFFF0;
  if (uval == 0) PS.Flags.Z = 1;
  PS.Flags.N = GET_SIGN_W(uval);
  if (!((val1 ^ val2) & 0x8000) && ((val2 ^ uval) & 0x8000)) {
    PS.Flags.V = 1;
  }
  if ((val1 + val2) > 0xFFFF) { // >=
    PS.Flags.C = 1;
  }
  memwrite16(da, uval);
}

static void SUB(uint32_t instr) {
  //istat[6]++;
  uint32_t d = instr & 077;
  uint32_t s = (instr & 07700) >> 6;
  //uint8_t l = 2 - (instr >> 15);  
  uint32_t src1 = memread16(aget(s, 2));  
  uint32_t da = aget(d, 2);
  uint32_t src2 = memread16(da);
  uint32_t dst = (src2 - src1) & 0xFFFF;
  PS.Word &= 0xFFF0;
  if (dst == 0) PS.Flags.Z = 1;
  if GET_SIGN_W(dst) {
    PS.Flags.N = 1;
  }
  if ((GET_SIGN_W(src1) != GET_SIGN_W(src2)) && (GET_SIGN_W(src1) == GET_SIGN_W(dst))) {
    PS.Flags.V = 1;
  }
  if (src1 > src2) {
    PS.Flags.C = 1;
  }
  memwrite16(da, dst);
  /*
  if (PC == 012000 || PC == 012002) {
    Serial.printf("PC: %06o, src1: %06o, src2: %06o, dest: %06o, da: %06o\r\n", 
      PC, src1, src2, dst, da);
    Serial.printf("PC: %06o, xor1: %06o, comp: %06o, xor2: %06o\r\n",
      PC, uint16_t(src1 ^ src2), uint16_t(~src2), uint16_t(uint16_t(~src2) ^ dst));
  }
  */
}

static void JSR(uint32_t instr) {
  //istat[7]++;
  uint32_t d = instr & 077;
  uint32_t s = (instr & 07700) >> 6;
  uint32_t l = 2 - (instr >> 15);
  uint32_t dst = aget(d, l);
  if (isReg(dst)) {
    longjmp(trapbuf, INTINVAL);
    //Serial.println(F("JSR called on register"));
    //panic();
  }
  push(R[s & 7]);
  /* XXX STKL
  if (!curuser) { // kernel mode
    if (R[6] < unibus::SLR + STKL_Y) {
      TRAP_REQ = 4;
    }
  }
  */
  R[s & 7] = R[7]; 
  R[7] = dst;
  //Serial.printf("jsr %06o\r\n", uval);
}

static void MUL(const uint32_t instr) {
  //istat[8]++;
  uint32_t d = instr & 077;
  uint32_t s = (instr & 07700) >> 6;
  if (s == 0 && d == 0) {
        longjmp(trapbuf, INTINVAL);
  }
  int32_t src = R[s & 7];
  uint32_t l = 2 - (instr >> 15);
  uint32_t da = aget(d, l);
  int32_t src2 = memread16(da);
  PS.Word &= 0xFFF0;
  // supnik
  if (GET_SIGN_W (src2))
      src2 = src2 | ~077777;
  if (GET_SIGN_W (src))
      src = src | ~077777;
  int32_t dst = src * src2;  
  if ((s & 1) == 0) { // even store both
    R[s & 7] = (dst >> 16) & 0177777;
    R[(s & 7) | 1] = dst   & 0177777;  
  } else {
    R[s & 7] = dst & 0177777; // low order product?
  }
  if (dst < 0) PS.Flags.N = 1;
  if (dst == 0) PS.Flags.Z = 1;
  // if ((dst >= 077777) || (dst < -077777)) PS |= FLAGC; rk11 works
  if ((dst >= 077777) || (dst < -0100000)) PS.Flags.C = 1; // more correct?
}

static void DIV(uint32_t instr) {
  //istat[9]++;
  uint32_t d = instr & 077;
  uint32_t s = (instr & 07700) >> 6;
  int32_t src = (R[s & 7] << 16) | (R[(s & 7) | 1]);
  uint32_t l = 2 - (instr >> 15);
  uint32_t da = aget(d, l);
  int32_t src2 = memread16(da);
  PS.Word &= 0xFFF0;
  if (src2 == 0) {
    PS.Flags.Z = PS.Flags.V = PS.Flags.C = 1; // supnik
    return;
  }
  // supnik too
  if ((((uint32_t) src) == 020000000000) && (src2 == 0177777)) {
    PS.Flags.V = 1;
    return;
  }
  if (GET_SIGN_W (src2)) {
    src2 = src2 | ~077777;
  }
  if (GET_SIGN_W (R[s & 7])) {
    src = src | ~017777777777;
  }
  int32_t dst = src /src2;
  if (dst < 0) {
    PS.Flags.N = 1;
  }
  if ((dst > 077777 || (dst < -0100000))) {
    PS.Flags.V = 1;
    return;
  }
  if ((src / src2) >= 0x10000) {
    PS.Flags.V = 1;
    return;
  }
  if (dst == 0) PS.Flags.Z = 1;
  R[s & 7] = dst & 0177777;
  R[(s & 7) | 1] = (src - (src2 * dst)) & 0177777;
}

static void ASH(uint32_t instr) {
  //istat[10]++;
  uint32_t d = instr & 077;
  uint32_t s = (instr & 07700) >> 6;
  uint32_t val1 = R[s & 7];
  uint32_t da = aget(d, 2);
  uint32_t val2 = memread16(da) & 077;
  PS.Word &= 0xFFF0;
  int32_t sval;
  if (val2 & 040) {
    val2 = (077 ^ val2) + 1;
    if (val1 & 0100000) {
      sval = 0xFFFF ^ (0xFFFF >> val2);
      sval |= val1 >> val2;
    }
    else {
      sval = val1 >> val2;
    }
    if (val1 & (1 << (val2 - 1))) {
      PS.Flags.C = 1;
    }
  } else {
    sval = (val1 << val2) & 0xFFFF;
    if (val1 & (1 << (16 - val2))) {
      PS.Flags.C = 1;
    }
  }
  R[s & 7] = sval;
  if (sval == 0) PS.Flags.Z = 1;
  if (sval & 0100000) {
    PS.Flags.N = 1;
  }
  if ((sval & 0100000) xor (val1 & 0100000)) {
    PS.Flags.V = 1;
  }
}

static void ASHC(uint32_t instr) {
  //istat[11]++;
  uint32_t d = instr & 077;
  uint32_t s = (instr & 07700) >> 6;
  uint32_t val1 = R[s & 7] << 16 | R[(s & 7) | 1]; // was uint16_t
  uint32_t da = aget(d, 2);
  uint32_t val2 = memread16(da) & 077;
  PS.Word &= 0xFFF0;
  int32_t sval;
  if (val2 & 040) {
    val2 = (077 ^ val2) + 1;
    if (val1 & 0x80000000) {
      sval = 0xFFFFFFFF ^ (0xFFFFFFFF >> val2);
      sval |= val1 >> val2;
    } else {
      sval = val1 >> val2;
    }
    if (val1 & (1 << (val2 - 1))) {
      PS.Flags.C = 1;
    }
  } else {
    sval = (val1 << val2) & 0xFFFFFFFF;
    if (val1 & (1 << (32 - val2))) {
      PS.Flags.C = 1;
    }
  }
  R[s & 7] = (sval >> 16) & 0xFFFF;
  R[(s & 7) | 1] = sval & 0xFFFF;
  if (sval == 0) PS.Flags.Z = 1;
  if (sval & 0x80000000) {
    PS.Flags.N = 1;
  }
  if ((sval & 0x80000000) xor (val1 & 0x80000000)) {
    PS.Flags.V = 1;
  }
}

static void XOR(uint32_t instr) {
  //istat[12]++;
  const uint32_t d = instr & 077;
  const uint32_t s = (instr & 07700) >> 6;
  const uint32_t val1 = R[s & 7];
  const uint32_t da = aget(d, 2);
  const uint32_t val2 = memread16(da);
  const uint32_t uval = val1 ^ val2;
  PS.Word &= 0xFFF1;
  if (uval == 0) PS.Flags.Z = 1;
  if (uval & 0x8000) {
    PS.Flags.N = 1;
  }
  memwrite16(da, uval);
}

static void SOB(const uint32_t instr) {
  //istat[13]++;
  const uint32_t s = (instr & 07700) >> 6;
  uint32_t o = instr & 0xFF;
  R[s & 7]--;
  if (R[s & 7]!=0) {
    o &= 077;
    o <<= 1;
    R[7] -= o;
  }
}

static void CLR(uint32_t instr) {
  //istat[14]++;
  const uint32_t d = instr & 077;
  const uint32_t l = 2 - (instr >> 15);
  PS.Word &= 0xFFF0;
  PS.Flags.Z = 1;
  memwrite(aget(d, l), l, 0);
  //Serial.printf("clr R0: %06o\r\n", R[0]);
}

static void COM(uint32_t instr) {
  //istat[15]++;
  uint32_t d = instr & 077;
  //uint8_t s = (instr & 07700) >> 6;
  uint32_t l = 2 - (instr >> 15);
  uint32_t msb = l == 2 ? 0x8000 : 0x80;
  uint32_t max = l == 2 ? 0xFFFF : 0xFF;
  uint32_t da = aget(d, l);
  uint32_t uval = memread(da, l) ^ max;
  PS.Word &= 0xFFF0;
  PS.Flags.C = 1;
  if (uval & msb) {
    PS.Flags.N = 1;
  }
  if (uval == 0) PS.Flags.Z = 1;
  memwrite(da, l, uval);
}

static void INC(const uint32_t instr) {
  //istat[16]++;
  const uint32_t d = instr & 077;
  const uint32_t l = 2 - (instr >> 15);
  const uint32_t msb = l == 2 ? 0x8000 : 0x80;
  const uint32_t max = l == 2 ? 0xFFFF : 0xFF;
  const uint32_t da = aget(d, l);
  const uint32_t uval = (memread(da, l) + 1) & max;
  PS.Word &= 0xFFF1;
  if (uval & msb) {
    PS.Flags.N = 1;
    PS.Flags.V = 1;
  }
  if (uval == 0) PS.Flags.Z = 1;
  memwrite(da, l, uval);
}

static void _DEC(uint32_t instr) {
  //istat[17]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  uint32_t msb = l == 2 ? 0x8000 : 0x80;
  uint32_t max = l == 2 ? 0xFFFF : 0xFF;
  uint32_t maxp = l == 2 ? 0x7FFF : 0x7f;
  uint32_t da = aget(d, l);
  uint32_t uval = (memread(da, l) - 1) & max;
  PS.Word &= 0xFFF1;
  if (uval & msb) {
    PS.Flags.N = 1;
  }
  if (uval == maxp) {
    PS.Flags.V = 1;
  }
  if (uval == 0) PS.Flags.Z = 1;
  memwrite(da, l, uval);
}

static void NEG(uint32_t instr) {
  //istat[18]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  uint32_t msb = l == 2 ? 0x8000 : 0x80;
  uint32_t max = l == 2 ? 0xFFFF : 0xFF;
  uint32_t da = aget(d, l);
  int32_t sval = (-memread(da, l)) & max;
  PS.Word &= 0xFFF0;
  if (sval & msb) {
    PS.Flags.N = 1;
  }
  if (sval == 0) {
    PS.Flags.Z = 1;
  } else {
    PS.Flags.C = 1;
  }
  if (sval == 0x8000) {
    PS.Flags.V = 1;
  }
  memwrite(da, l, sval);
}

static void ADC(uint32_t instr) {
  //istat[19]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  uint32_t msb = l == 2 ? 0x8000 : 0x80;
  uint32_t max = l == 2 ? 0xFFFF : 0xFF;
  uint32_t da = aget(d, l);
  uint32_t uval = memread(da, l);
  if (PS.Flags.C) {
    PS.Word &= 0xFFF0;
    if ((uval + 1) & msb) {
      PS.Flags.N = 1;
    }
    if (uval == max) PS.Flags.Z = 1;
    if (uval == 0077777) {
      PS.Flags.V = 1;
    }
    if (uval == 0177777) {
      PS.Flags.C = 1;
    }
    memwrite(da, l, (uval + 1)&max);
  } else {
    PS.Word &= 0xFFF0;
    if (uval & msb) {
      PS.Flags.N = 1;
    }
    if (uval == 0) PS.Flags.Z = 1;
  }
}

static void SBC(uint32_t instr) {
  //istat[20]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  //uint16_t msb = l == 2 ? 0x8000 : 0x80;
  uint32_t max = l == 2 ? 0xFFFF : 0xFF;
  uint32_t da = aget(d, l);
  uint32_t dst = memread(da, l);
  PS.Word &= 0xFFF1;
  uint32_t res = (dst - PS.Flags.C);
  if (GET_SIGN_W(res)) PS.Flags.N = 1;
  if (res == 0) PS.Flags.Z = 1;
  if (dst == 0100000) PS.Flags.V = 1;
  if (dst != 0) {
    PS.Flags.C = 0;
  }
  memwrite(da, l, (res & max));
}

static void TST(uint32_t instr) {
  //istat[21]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15); // result is 0 if word addressed, else 1
  uint32_t msb = l == 2 ? 0x8000 : 0x80; // l == 1?
  uint32_t uval = memread(aget(d, l), l);
  PS.Word &= 0xFFF0;
  if (uval & msb) {
    PS.Flags.N = 1;
  }
  if (uval == 0) PS.Flags.Z = 1;
}

static void ROR(uint32_t instr) {
  //istat[22]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  uint32_t da = aget(d, l);
  uint32_t src = memread(da, l);
  uint32_t dst;
  if (l == 2) {
    dst = (src >> 1);
    dst |= (PS.Flags.C << 15);
    PS.Flags.N = GET_SIGN_W(dst); 
  } else {
    dst = ((src & 0xFF) >> 1);
    dst |= (PS.Flags.C << 7);
    PS.Flags.N = GET_SIGN_B(dst);
  }
  if (dst == 0) PS.Flags.Z = 1;
  PS.Flags.C = src & 1;
  PS.Flags.V = PS.Flags.N ^ PS.Flags.C;
  memwrite(da, l, dst);
}

static void ROL(uint32_t instr) {
  //istat[23]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  uint32_t da = aget(d, l);
  uint32_t src = memread(da, l);
  uint32_t dst;
  if (l == 2) {
    dst = (src << 1) | PS.Flags.C;
    PS.Flags.N = GET_SIGN_W(dst);
    PS.Flags.C = GET_SIGN_W(src);
  } else {
    dst = ((src << 1) | PS.Flags.C) & 0xFF;
    PS.Flags.N = GET_SIGN_B(dst);
    PS.Flags.C = GET_SIGN_B(src);
  }
  if (dst == 0) PS.Flags.Z = 1;
  PS.Flags.V = PS.Flags.N ^ PS.Flags.C;
  memwrite(da, l, dst);
}

static void ASR(uint32_t instr) {
  //istat[24]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  uint32_t msb = l == 2 ? 0x8000 : 0x80;
  uint32_t da = aget(d, l);
  uint32_t src = memread(da, l);
  uint32_t dst = src >> 1 | (src & msb);
  PS.Word &= 0xFFF0;
  if (l == 2) {
    if (GET_SIGN_W(dst)) {
      PS.Flags.N = 1;
    }
  } else {
    if (GET_SIGN_B(dst)) {
      PS.Flags.N = 1;
    }
  }
  if (dst == 0) {
    PS.Flags.Z = 1;
  }
  if (dst & 1) {
    PS.Flags.C = 1;
  }
  PS.Flags.V = (PS.Flags.N ^ PS.Flags.C);
  memwrite(da, l, dst);
}

static void ASL(uint32_t instr) {
  //istat[25]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  uint32_t msb = l == 2 ? 0x8000 : 0x80;
  uint32_t max = l == 2 ? 0xFFFF : 0xff;
  uint32_t da = aget(d, l);
  // TODO(dfc) doesn't need to be an sval
  int32_t sval = memread(da, l);
  PS.Word &= 0xFFF0;
  if (sval & msb) {
    PS.Flags.C = 1;
  }
  if (sval & (msb >> 1)) {
    PS.Flags.N = 1;
  }
  if ((sval ^ (sval << 1)) & msb) {
    PS.Flags.V = 1;
  }
  sval = (sval << 1) & max;
  if (sval == 0) PS.Flags.Z = 1;
  memwrite(da, l, sval);
}

static void SXT(uint32_t instr) {
  //istat[26]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  uint32_t da = aget(d, l);
  PS.Flags.V = 0; // PDP11/45 behaviour, SIMH also clears
  if (PS.Flags.N) {
    memwrite(da, l, 0177777);
    PS.Flags.Z = 0;
  } else {
    PS.Flags.Z = 1;
    memwrite(da, l, 0);
  }
}

static void JMP(uint32_t instr) {
  //istat[27]++;
  uint32_t d = instr & 077;
  uint32_t uval = aget(d, 2);
  if (isReg(uval)) {
    //Serial.println(F("JMP called with register dest"));
    //panic();
    longjmp(trapbuf, INTINVAL);
  }
  R[7] = uval;
  //Serial.printf("jmp %06o\r\n", uval);
}

static void SWAB(uint32_t instr) {
  //istat[28]++;
  uint32_t d = instr & 077;
  uint32_t l = 2 - (instr >> 15);
  uint32_t da = aget(d, l);
  uint32_t uval = memread(da, l);
  uval = ((uval >> 8) | (uval << 8)) & 0xFFFF;
  PS .Word &= 0xFFF0;
  if(uval & 0xFF) PS.Flags.Z = 1;
  if (uval & 0x80) {
    PS.Flags.N = 1;
  }
  memwrite(da, l, uval);
}

static void MARK(uint32_t instr) {
  //istat[29]++;
  R[6] = R[7] + ((instr & 077) << 1);
  R[7] = R[5];
  R[5] = pop();
}

static void MFPI(uint32_t instr) {
  //istat[30]++;
  uint32_t d = instr & 077;
  uint32_t da = aget(d, 2);
  uint32_t uval = 0;
  if (da == 0170006) {
    // val = (curuser == prevuser) ? R[6] : (prevuser ? k.USP : KSP);
    if (curuser == prevuser) {
      uval = R[6];
    } else {
      if (prevuser) {
        uval = USP;
      } else {
        uval = KSP;
      }
    }
  } else if (isReg(da)) {
    Serial.println(F("invalid MFPI instruction"));
    longjmp(trapbuf, INTINVAL);
  } else {
    uval = unibus::read16(mmu::decode((uint16_t)da, false, prevuser));
  }
  push(uval);
  /* XXX STKL
  if (!curuser) { // kernel mode
    if (R[6] < unibus::SLR + STKL_Y) {
      TRAP_REQ = 4;
    }
  }
  */
  PS.Word &= 0xFFF0;
  PS.Flags.C = 1;
  if (uval == 0) PS.Flags.Z = 1;
  if (uval & 0x8000) {
    PS.Flags.N = 1;
  }
}

static void MTPI(uint32_t instr) {
  //istat[31]++;
  uint32_t d = instr & 077;  // destination operand 
  uint32_t da = aget(d, 2); // destination address
  uint32_t uval = pop();
  if (da == 0170006) {
    if (curuser == prevuser) {
      R[6] = uval;
    } else {
      if (prevuser) {
        USP = uval;
      } else {
        KSP = uval;
      }
    }
  } else if (isReg(da)) {
    //Serial.println(F("invalid MTPI instruction")); 
    //longjmp(trapbuf, INTINVAL);
    R[da & 7] = uval;
  } else {
    unibus::write16(mmu::decode((uint16_t)da, true, prevuser), uval);
  }
  PS.Word &= 0xFFF0;
  PS.Flags.Z = (uval == 0);
  PS.Flags.N = GET_SIGN_W(uval);
}

static void RTS(uint32_t instr) {
  //istat[32]++;
  uint32_t d = instr & 077;
  R[7] = R[d & 7];
  R[d & 7] = pop();
  //Serial.printf("rts %06o\r\n", R[7]);
}

static void EMTX(uint32_t instr) {
  //istat[33]++;
  uint32_t uval;
  if ((instr & 0177400) == 0104000) { // EMT
    uval = 030; // trap vector (PC), new PS is 032;
  } else if ((instr & 0177400) == 0104400) { // TRAP
    uval = 034;
  } else if (instr == 3) { // BPT
    uval = 014;
  } else { // IOT
    uval = 020;
  }
  uint32_t prev = PS.Word;
  switchmode(false);
  push(prev);
  push(R[7]);
  R[7] = unibus::read16(uval);
  PS.Word = unibus::read16(uval + 2);
  if (prevuser) {
    PS.Word |= (1 << 13) | (1 << 12);
  }
}

static void RTT(uint32_t instr) {
  //istat[34]++;
  R[7] = pop();
  uint32_t uval = pop();
  if (curuser) {
    uval &= 047;
    uval |= PS.Word & 0177730;
  }
  unibus::write16(0777776, uval);
  //Serial.printf("rts %06o\r\n", R[7]);
}

static void RESET(uint32_t instr) {
  //istat[35]++;
  if (curuser) {
    return;
  }
  //unibus::SLR = 0;
  //mmu::SR0 = 0;
  dl11::reset();
  rk11::reset();
  tm11::reset();
}

#define PRINTSTATE 0
void step() {
  PC = R[7];
  const uint32_t instr = unibus::read16(mmu::decode(PC, false, curuser));
  R[7] += 2;
  if (trace > 0 || PRINTSTATE) {
    trace--;
    print_state();
  }

  switch (instr & 0070000) {
    case 0010000: // MOV
      MOV(instr);
      return;
    case 0020000: // CMP
      CMP(instr);
      return;
    case 0030000: // BIT
      BIT(instr);
      return;
    case 0040000: // BIC
      BIC(instr);
      return;
    case 0050000: // BIS
      BIS(instr);
      return;
  }
  switch (instr & 0170000) {
    case 0060000: // ADD
      ADD(instr);
      return;
    case 0160000: // SUB
      SUB(instr);
      return;
  }
  switch (instr & 0177000) {
    case 0004000: // JSR
      JSR(instr);
      return;
    case 0070000: // MUL
      MUL(instr);
      return;
    case 0071000: // DIV
      DIV(instr);
      return;
    case 0072000: // ASH
      ASH(instr);
      return;
    case 0073000: // ASHC
      ASHC(instr);
      return;
    case 0074000: // XOR
      XOR(instr);
      return;
    case 0077000: // SOB
      SOB(instr);
      return;
  }
  switch (instr & 0077700) {
    case 0005000: // CLR
      CLR(instr);
      return;
    case 0005100: // COM
      COM(instr);
      return;
    case 0005200: // INC
      INC(instr);
      return;
    case 0005300: // DEC
      _DEC(instr);
      return;
    case 0005400: // NEG
      NEG(instr);
      return;
    case 0005500: // ADC
      ADC(instr);
      return;
    case 0005600: // SBC
      SBC(instr);
      return;
    case 0005700: // TST
      TST(instr);
      return;
    case 0006000: // ROR
      ROR(instr);
      return;
    case 0006100: // ROL
      ROL(instr);
      return;
    case 0006200: // ASR
      ASR(instr);
      return;
    case 0006300: // ASL
      ASL(instr);
      return;
    case 0006700: // SXT
      SXT(instr);
      return;
  }
  switch (instr & 0177700) {
    case 0000100: // JMP
      JMP(instr);
      return;
    case 0000300: // SWAB
      SWAB(instr);
      return;
    case 0006400: // MARK
      MARK(instr);
      break;
    case 0006500: // MFPI
      MFPI(instr);
      return;
    case 0006600: // MTPI
      MTPI(instr);
      return;
  }
  if ((instr & 0177770) == 0000200) { // RTS
    RTS(instr);
    return;
  }

  switch (instr & 0177400) { // 177400
    case 0000400: // BR
      branch(instr & 0xFF); 
      return;
    case 0001000: // BNE
      if (!PS.Flags.Z) {
        branch(instr & 0xFF);
      }
      return;
    case 0001400: // BEQ
      if (PS.Flags.Z) {
        branch(instr & 0xFF);
      }
      return;
    case 0002000: // BGE
      if (!(PS.Flags.N ^ PS.Flags.V)) {
        branch(instr & 0xFF);
      }
      return;
    case 0002400: // BLT
      if (PS.Flags.N ^ PS.Flags.V) { 
        branch(instr & 0xFF);
      }
      return;
    case 0003000: // BGT
      if (!(PS.Flags.Z || (PS.Flags.N ^ PS.Flags.V ))) {
        branch(instr & 0xFF);
      }
      return;
    case 0003400: // BLE
      if (PS.Flags.Z || (PS.Flags.N ^ PS.Flags.V)) {
        branch(instr & 0xFF);
      }
      return;
    case 0100000: // BPL
      if (!PS.Flags.N) {
        branch(instr & 0xFF);
      }
      return;
    case 0100400: // BMI
      if (PS.Flags.N) {
        branch(instr & 0xFF);
      }
      return;
    case 0101000: // BHI
      if (!(PS.Flags.C || PS.Flags.Z))  {
        branch(instr & 0xFF);
      }
      return;
    case 0101400: // BLOS
      if (PS.Flags.C || PS.Flags.Z) {
        branch(instr & 0xFF);
      }
      return;
    case 0102000: // BVC
      if (!PS.Flags.V) {
        branch(instr & 0xFF);
      }
      return;
    case 0102400: // BVS
      if (PS.Flags.V) {
        branch(instr & 0xFF);
      }
      return;
    case 0103000: // BCC
      if (!PS.Flags.C) {
        branch(instr & 0xFF);
      }
      return;
    case 0103400: // BCS
      if (PS.Flags.C) {
        branch(instr & 0xFF);
      }
      return;
  }
  if (((instr & 0177000) == 0104000) || (instr == 3) || (instr == 4)) { // EMT TRAP IOT BPT
    EMTX(instr);
    return;
  }
  if ((instr & 0177740) == 0240) { // CL?, SE?
    if ((instr & 020) == 020) {
      PS.Word |= instr & 017;
    } else {
      PS.Word &= ~(instr & 017);
    }
    return;
  }
  switch (instr) {
    case 0000000: // HALT
      if (curuser) {
        break;
      }
      Serial.println(F("HALT"));
      panic();
      return;
    case 0000001: // WAIT
      if (curuser) {
        break;
      }
      return;
    case 0000002: // RTI
    case 0000006: // RTT
      RTT(instr);
      return;
    case 0000005: // RESET
      RESET(instr);
      return;
  }
  if (instr == 0170011) { // SETD ; not needed by UNIX, but used; therefore ignored
    return;
  }
#ifdef INVLOG
  invlog.printf("invalid instruction: %06o: %06o\n", PC, instr);
  invlog.flush();
#endif
  //print_state();
  longjmp(trapbuf, INTINVAL);
}

void trapat(const uint16_t vec) { // , msg string) {
  if (vec & 1) {
    Serial.println(F("trapat() with an odd vector number?"));
    panic();
  }
  yield();
  //Serial.print(F("trap: ")); Serial.println(vec, OCT);

  uint16_t prev = PS.Word;
  switchmode(false);
  push(prev);
  push(R[7]);

  R[7] = unibus::read16(vec);
  PS.Word = unibus::read16(vec + 2);
  if (prevuser) {
    PS.Word |= (1 << 13) | (1 << 12);
  }
}

void interrupt(const uint8_t vec, const uint8_t pri) {
  //yield();
  if (vec & 1) {
    Serial.printf("interrupt vector is odd: %06o\r\n", vec);
    panic();
  }
  __disable_irq();
  // fast path
  if (itab[0].vec == 0) {
    itab[0].vec = vec;
    itab[0].pri = pri;
    __enable_irq();
    return;
  }
  uint8_t i;
  for (i = 0; i < ITABN; i++) {
    if ((itab[i].vec == 0) || (itab[i].pri < pri)) {
      break;
    }
  }
  for (; i < ITABN; i++) {
    if ((itab[i].vec == 0) || (itab[i].vec >= vec)) {
      break;
    }
  }
  if (i >= ITABN) {
    Serial.println(F("interrupt table full")); 
    for (int i = 0; i < ITABN; i++) {
      Serial.printf("itab[%d] vec: %d, pri: %d\r\n", i, itab[i].vec, itab[i].pri);
    }
    __enable_irq();
    panic();
  }
  for (int j = i + 1; j < ITABN; j++) {
    itab[j] = itab[j - 1];
  }
  itab[i].vec = vec;
  itab[i].pri = pri;
  __enable_irq();
}

// pop the top interrupt off the itab.
static void popirq() {
  __disable_irq();
  for (int i = 0; i < ITABN - 1; i++) {
    itab[i] = itab[i + 1];
  }
  itab[ITABN - 1].vec = 0;
  itab[ITABN - 1].pri = 0;
  __enable_irq();
}

void handleinterrupt() {
  __disable_irq();
  const uint32_t vec = itab[0].vec;
  if (DEBUG_INTER && vec != 0100) {
    for (int i = 0; i < ITABN; i++) {
      Serial.printf("%x: %03o/%1o, ", i, itab[i].vec, itab[i].pri);
      if (i == 7) {
        Serial.println();
      }
    }
    Serial.println();
  }
  __enable_irq();
  const uint32_t trapvec = setjmp(trapbuf);
  if (trapvec == 0) {
    uint32_t prev = PS.Word;
    switchmode(false);
    push(prev);
    push(R[7]);
  } else {
    trapat(trapvec);
  }

  R[7] = unibus::read16(vec);
  PS.Word = unibus::read16(vec + 2);
  if (prevuser) {
    PS.Word |= (1 << 13) | (1 << 12);
  }
  popirq();
}

};
