#include <Arduino.h>

// PDP11/40
// KD11-A CPU
// KD11-D MMU
// KW11-L CLOCK

// interrupts
enum {
  INTBUS    = 0004,
  INTINVAL  = 0010,
  INTDEBUG  = 0014,
  INTIOT    = 0020,
  INTTTYIN  = 0060,
  INTTTYOUT = 0064,
  INTCLOCK  = 0100,
  INTRK     = 0220,
  INTTM     = 0224,
  INTFAULT  = 0250,
};

enum {
  PRINTSTATE = false,
  INSTR_TIMING = false,
  DEBUG_INTER = false,
  DEBUG_RK05 = false,
  DEBUG_MMU = false,
  ENABLE_LKS = true,
};

void print_state();
void toggle_trace();
void panic();
uint32_t disasm(uint32_t ia);
void trap(uint16_t num);