#include <CLI.h>

#include <RTClib.h>

#include "cpu.h"
#include "mmu.h"
#include "unibus.h"
#include "rk05.h"
#include "tm11.h"
#include "console.h"
#include "pdp11.h"
#include "TFTPService.h"

#include "SdFat.h"

//extern SdFat sd;
extern SdFs sd;
extern RTC_DS3231 rtc;
extern uint32_t ips;
extern int trace;

#if defined(TEENSYDUINO)
  #define SPIWIFI        SPI  // The SPI port
  #define SPIWIFI_CS     5    // Chip select pin
  #define SPIWIFI_RST    6    // Reset pin
  #define SPIWIFI_RDY    9    // a.k.a BUSY or READY pin
  #define SPIWIFI_GPIO  -1    // 10
#else 
#error "Please define the pins according to your board / setup."
#endif

#define SerialESP32 Serial1

tftp::TFTPService tftpService = tftp::TFTPService();
int status = WL_IDLE_STATUS;     // the Wifi radio's status

namespace console {

bool active = false;
bool tftpActive = false;
char *ssid;
char *pass;

uint32_t mem_addr = 0;
uint32_t dis_addr = 0;

void reset_machine(void) {
  SCB_AIRCR = 0x05FA0004;
}

uint32_t dump_mem(CLIClient *dev, u_int32_t addr) {
  int x,y;
  for (y = 0; y <  8; y++) {
    dev->printf("%06o: ", addr);
    for (x = 0; x < 16; x+=2) {
      if ((addr + x) < 0760000) {
        dev->printf("%06o ", unibus::read16(addr + x));
      } else {
        dev->println("io addr, use Examine");
        return 0;
      }
    }
    dev->println();
    addr += x;
  }
  return addr;
}

CLI_COMMAND(connectHandler) {
  dev->println();
  return 0;
}

CLI_COMMAND(lsCmd) {
  if (argc == 2 && *argv[1] != '-') {
    sd.ls(argv[1], LS_DATE|LS_SIZE);
  } else {
    sd.ls(LS_DATE|LS_SIZE);
  }
  return 0;
}

CLI_COMMAND(ptCmd) {
  uint16_t addr;
  uint16_t count;
  uint8_t data;
  uint8_t chks;
  uint8_t accu;
  if (argc != 2) {
    dev->println("Usage: pt filename");
    return 1;
  }
  FsFile pt = sd.open(argv[1], O_READ);
  if (!pt) {
    dev->print("could not open "); dev->println(argv[1]);
    return 2;
  }
  while(pt.available()) {
    // header
    data = pt.read();
    if (data != 1) {
      continue;
    }
    accu = data;
    data = pt.read(); // null frame
    if (data != 0) {
      dev->printf("pt: null frame is %03o\r\n", data);
    }
    accu += data;
    pt.read(&count, 2);
    accu += (count >> 8);
    accu += count & 0377;
    pt.read(&addr, 2);
    accu += (addr >> 8);
    accu += addr & 0377;
    if (count == 6) {
      dev->printf("pt: start addr: %06o\r\n", addr);
      break;
    }
    dev->printf("pt: block header: addr: %06o, count %d (%06o)\r\n", addr, count, count);
    count-=6;
    while(count && pt.available()) {
      data = pt.read();
      accu += data;
      //dev->printf("pt: unibus write: %06o: %03o\r\n", addr, data);
      unibus::write8(addr, data);
      addr++;
      count--;
    }
    chks = pt.read();
    dev->printf("pt: block checksum: %03o, accu: %03o, %s\r\n", 
    chks, accu, ((uint8_t) (chks + accu) == 0) ?  "ok" : "incorrect");
  }
  pt.close();
  return 0;
}

CLI_COMMAND(catCmd) {
  if (argc != 2) {
    dev->println("Usage: cat filename");
    return 1;
  }
  FsFile file = sd.open(argv[1], O_READ);;
  if (!file) {
    dev->print("could not open "); dev->println(argv[1]);
    return 2;
  } else {
    while(file.available()) {
      String s = file.readStringUntil('\n');
      Serial.println(s);
    }
    file.close();
  }
  return 0;
}

CLI_COMMAND(bootCmd) {
  active = false;
  tftpService.end();
  WiFi.end();
  tftpActive = false;
  return 0;
}

CLI_COMMAND(contCmd) {
  active = false;
  tftpService.end();
  WiFi.end();
  tftpActive = false;
  return 0;
}

CLI_COMMAND(dateCmd) {
  if (rtc.begin()) {
    switch (argc) {
      case 1: {
        DateTime now = rtc.now();
        dev->printf("%02d.%02d.%04d %02d:%02d:%02d UTC\r\n", now.day(), now.month(), now.year(), now.hour(), now.minute(), now.second());
        break;
      }
      case 2: {
        DateTime dt(argv[1]);
        rtc.adjust(dt);
        dt = rtc.now();
        dev->printf("%02d.%02d.%04d %02d:%02d:%02d UTC\r\n", dt.day(), dt.month(), dt.year(), dt.hour(), dt.minute(), dt.second());
        break;
      }
      default: {
        dev->println("Usage: date 2020-06-25T15:29:37 (UTC)");
        return 1;      
        break;
      }
    }
    return 0;
  } else {
    dev->println("RTC_DS3231 not found. Function disabled.\r\n");
    return 1;
  }
}

CLI_COMMAND(patchCmd) {
  if (rtc.begin()) {
    patch_super = patch_super ? false : true;
    dev->printf("patch_super: %s\r\n", patch_super ? "true" : "false");
    return 0;
  } else {
    dev->println("RTC_DS3231 not found. Function disabled.\r\n");
    return 1;
  }
}

CLI_COMMAND(tmCmd) {
  char buf[15];
  if (argc == 1) {
    for (int i = 0; i < TM_NUM_DRV; i++) {
      if (tm11::tmdata[i].attached && tm11::tmdata[i].file.getName(&buf[0], sizeof(buf))) {
        dev->printf("tm%d: %s\r\n", i, buf);
      } else {
        dev->printf("tm%d: -\r\n", i);
      }
    }
    return 0;
  }
  if (argc != 3) {
    dev->println("Usage: tm devicenumber filename");
    return 1;
  }
  int tape = atoi(argv[1]);
  if (tape > 4) {
    dev->printf("max tape number is 1\r\n");
    return 2;
  }
  if (argv[2][0] == '-') {
    tm11::tmdata[tape].file.close();
    tm11::tmdata[tape].attached = false;
    tm11::reset();
    dev->printf("detached tm%d\r\n", tape);
    return 0;
  }
  if (!tm11::tmdata[tape].file.open(argv[2], O_RDWR)) {
    dev->printf("could not open %s\r\n", argv[2]);
    return 3;
  } else {
    tm11::tmdata[tape].attached = true;
    tm11::reset();
    dev->printf("attached %s on tm%d\r\n", argv[2], tape);
    return 0;
  }
}

CLI_COMMAND(rkCmd) {
  char buf[15];
  if (argc == 1) {
    for (int i = 0; i < RK_NUM_DRV; i++) {
      if (rk11::rkdata[i].attached && rk11::rkdata[i].file.getName(&buf[0], sizeof(buf))) {
        dev->printf("rk%d: %s\r\n", i, buf);
      } else {
        dev->printf("rk%d: -\r\n", i);
      }
    }
    return 0;
  }
      
  if (argc != 3) {
    dev->println("Usage: rk devicenumber filename");
    return 1;
  }
  int drive = atoi(argv[1]);
  if (drive > 4) {
    dev->printf("max drive number is 4\r\n");
    return 2;
  }
  if (argv[2][0] == '-') {
    rk11::rkdata[drive].file.close();
    rk11::rkdata[drive].attached = false;
    rk11::reset();
    dev->printf("detached rk%d\r\n", drive);
    return 0;
  }
  if (!rk11::rkdata[drive].file.open(argv[2], O_RDWR)) {
    dev->printf("could not open %s\r\n", argv[2]);
    return 3;
  } else {
    rk11::rkdata[drive].attached = true;
    rk11::reset();
    dev->printf("attached %s on rk%d\r\n", argv[2], drive);
    return 0;
  }
}

CLI_COMMAND(cpCmd) {
  if (argc != 3) {
    dev->println("Usage: cp src dst");
    return 1;
  }
  FsFile src;
  FsFile dst;
  if (!src.open(argv[1], O_READ)) {
    dev->println("could not read src file");
    return 2;
  }
  if (!dst.open(argv[2], O_CREAT|O_TRUNC|O_WRITE)) {
    dev->println("could not write src file");
    return 3;
  }
  size_t n;  
  uint8_t buf[2048];
  int col = 0;
  while ((n = src.read(buf, sizeof(buf))) > 0) {
    dst.write(buf, n);
    dev->print(".");
    col++;
    if (col > 72) {
      dev->println();
      col = 0;
    }
  }
  dev->println();
  if (!src.close()) {
    return 4;
  }
  if (!dst.close()) {
    return 5;
  }
  return 0;
}

CLI_COMMAND(mvCmd) {
  if (argc != 3) {
    dev->println("Usage: mv old new");
    return 1;
  }
  bool r = sd.rename(argv[1], argv[2]);
  if (!r) {
    dev->printf("mv: rename %s to %s failed\r\n", argv[1], argv[2]);
    return 2;
  }
  return 0;
}

CLI_COMMAND(rmCmd) {
  if (argc != 2) {
    dev->println("Usage: rm filename"); 
    return 1;
  }
  bool r = sd.remove(argv[1]);
  if (!r) {
    dev->printf("rm: %s: No such file or directory\r\n", argv[1]);
    return 2;
  }
  return 0;
}

CLI_COMMAND(dCmd) {
  int i;
  int res;
  unsigned int val;
  switch(argc) {
    case 1:
      for (i = 0; i < 16; i++) {
        dis_addr = disasm(dis_addr);
      }
      return 0;
    case 2:
    res = sscanf(argv[1], "%06o", &val);
    if (res == 1) {
      dis_addr = val & ~ 1;
      for (i = 0; i < 16; i++) {
        dis_addr = disasm(dis_addr & ~1);
      }
      return 0;
    } else {
      return 1;
    }
    break;
  }
  return 2;
}

CLI_COMMAND(mCmd) {
  int res;
  unsigned int val;
  switch (argc) {
    case 1:
      mem_addr = dump_mem(dev, mem_addr & ~1);
      return 0;
      break;
    case 2:
      res = sscanf(argv[1], "%06o", &val);
      if (res == 1) {        
        mem_addr = dump_mem(dev, val & ~1);
        return 0;
      } else {
        return 1;
      }
      break;
  }
  return 2;
}

CLI_COMMAND(eCmd) {
  int res;
  unsigned int addr;
  if (argc == 2) {
    res = sscanf(argv[1], "%06o", &addr);
    if (res == 1) {
      dev->printf("%06o: %06o\r\n", addr & ~1, unibus::read16(addr & ~1));
      return 0;
    }   
  }
  return 1;
}

CLI_COMMAND(wCmd) {
  unsigned int addr;
  unsigned int data;
  int res, i;
  switch (argc) {
    case 1:
    dev->println("addr?");
    return 1;
    break;
    case 2:
    dev->println("data?");
    return 2;
    break;
    default:
      res = sscanf(argv[1], "%06o", &addr);
      if (res != 1) {
        dev->println("addr?");
        return 3;
      }
      addr &= ~1; // make even
      for (i = 2; i < min(argc, 10); i++) {
        res = sscanf(argv[i], "%06o", &data);
        if (res != 1) {
          dev->println("data?");
          return 4;
        }
        unibus::write16(addr, (uint16_t) data);
        addr+=2;
      }          
  }
  return 0;
}

CLI_COMMAND(gCmd) {
  unsigned int val;
  int res;
  if (argc == 2) {
    res = sscanf(argv[1], "%06o", &val);
    if (res == 1) {
      cpu::PC = (uint16_t) val;
      cpu::R[7] = (uint16_t) val;
      cpu::g_cmd = true;
      active = false;
      return 0;
    }
  } else {
    dev->println("addr?");
  }
  return 0;
}

CLI_COMMAND(traceCmd) {
  unsigned int n;
  int res;
  switch (argc) {
    case 1:
      dev->printf("trace: %d", trace);
      break;
    case 2:  
      res = sscanf(argv[1], "%d", &n);
      if (res == 1) {
        trace = n;
      }
      break;
  }
  return 0;
}

CLI_COMMAND(dumpCmd) {
  if (!unibus::dump()) {
    dev->println("core dump failed");
    return 1;
  }
  return 0;
}

CLI_COMMAND(tftpCmd) {
  // TFTPServer
  dev->println();
  dev->println("Initializing AirLift coprocessor.");
  WiFi.setPins(SPIWIFI_CS, SPIWIFI_RDY, SPIWIFI_RST, SPIWIFI_GPIO, &SPIWIFI);    
  WiFi.setLEDs(255, 0, 0);
  WiFi.setHostname("pdp1140");
  SerialESP32.begin(115200);
  delay(2500);
  WiFi.setLEDs(128, 32, 0);
  // check for the WiFi module:
  int i = 4;
  while(--i) {
    if (WiFi.status() == WL_NO_MODULE) {
      dev->println("Communication with WiFi module failed!");
    } else {
      break;
    }    
  }
  if (i == 0) {
    return 2;
  }
  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    dev->println("Please upgrade the AirLift firmware!");
  } else {
    dev->printf("AirLift firmware version is: %s\r\n", fv.c_str());
  }
  // attempt to connect to Wifi network:
  const char *home_ssid = ""; 
  const char *home_pass= "";
  switch (argc) {
    case 1:
      ssid = (char *) home_ssid;
      pass = (char *) home_pass;
      break;
    case 3:
      ssid = argv[1];
      pass = argv[2];
      break;
  default:
      dev->println("Usage: tftp ssid pass");
      return 1;
  }

  dev->printf("Connecting to WPA SSID: %s\r\n", ssid);
  while (status != WL_CONNECTED) { 
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, pass);
  }
  WiFi.setHostname("pdp1140");
  WiFi.setLEDs(0, 255, 0);
  // you're connected now, so print out the data:
  dev->printf("Connected to %s\r\n", ssid);
  IPAddress ip = WiFi.localIP();
  dev->print("IP Address: ");
  dev->println(ip);
  if (!tftpService.begin()) {
      dev->println("TFTPService not available.");
      return 3;
  }
  dev->println("TFTPService started.");
  tftpActive = true;
  return 0;
}

CLI_COMMAND(resetCmd) {
  reset_machine();
  return 0; // machine will reset anyway
}

CLI_COMMAND(helpCmd) {
  dev->println("available commands\n");
  dev->println("ls    - list directory of sd card"); 
  dev->println("pt    - load paper tape file");
  dev->println("        usage: pt filename");
  dev->println("cp    - copy file");
  dev->println("        usage: cp src dst");  
  dev->println("mv    - rename file");
  dev->println("        usage: mv old new");
  dev->println("rm    - remove file");
  dev->println("rk    - attach filename to rk11 drive number");
  dev->println("        usage: rk [0-7] filename, '-' detaches");
  dev->println("tm    - attach filename to tm11 drive number");
  dev->println("        usage: tm [0-7] filename, '-' detaches");
  dev->println("cat   - print file to standard output");
  dev->println("boot  - run machine bootstrap code");
  dev->println("cont  - continue after pause (^P)");
  dev->println("date  - set the rtc to iso8601 date (in utc)");
  dev->println("        usage: date 2020-06-25T15:29:37");
  dev->println("trace - set the trace counter to n instructions");
  dev->println("dump  - dump first 64kb to core file");
  dev->println("tftp  - start tftp service (console only)");
  dev->println("        usage: tftp [ssid] [pass]");
  dev->println("reset - reset machine");
  dev->println("patch - patch the rtc time into to superblock on read");
  dev->println("        use with V6 unix only (for now)");
  return 0;
}

void setup(bool brk) {
  active = true;
  CLI.setDefaultPrompt("$ ");
  CLI.onConnect(connectHandler);
  CLI.addClient(Serial);
  if (brk) {
    //Serial.printf("%06o %06o %06o %06o\r\n", cpu::R[0], cpu::R[4], cpu::R[6], cpu::R[7]); 
    Serial.println();   
    Serial.printf("%d instr/s, mmu: %s\r\n", ips, mmu::SR0 & 1 ? "on" : "off");    
    Serial.printf("R0 %06o ", uint16_t(cpu::R[0]));
    Serial.printf("R1 %06o ", uint16_t(cpu::R[1]));
    Serial.printf("R2 %06o ", uint16_t(cpu::R[2]));
    Serial.printf("R3 %06o\r\n", uint16_t(cpu::R[3]));
    Serial.printf("R4 %06o ", uint16_t(cpu::R[4]));
    Serial.printf("R5 %06o ", uint16_t(cpu::R[5]));
    Serial.printf("R6 %06o ", uint16_t(cpu::R[6]));
    Serial.printf("R7 %06o ", uint16_t(cpu::R[7]));
    Serial.printf("PS [%s%s%s%s%s%s]\r\n",
          cpu::prevuser ? "u" : "k",
          cpu::curuser ? "U" : "K",
          cpu::PS.Flags.N ? "N" : " ",
          cpu::PS.Flags.Z ? "Z" : " ",
          cpu::PS.Flags.V ? "V" : " ",
          cpu::PS.Flags.C ? "C" : " ");
  } 
  
  CLI.addCommand("D", dCmd);
  CLI.addCommand("E", eCmd);
  CLI.addCommand("M", mCmd);
  CLI.addCommand("W", wCmd);
  CLI.addCommand(">", wCmd);
  CLI.addCommand("G", gCmd);

  CLI.addCommand("ls", lsCmd);  
  CLI.addCommand("pt", ptCmd);
  CLI.addCommand("cp", cpCmd);
  CLI.addCommand("mv", mvCmd);
  CLI.addCommand("rm", rmCmd);
  CLI.addCommand("rk", rkCmd);  
  CLI.addCommand("tm", tmCmd);  
  CLI.addCommand("cat", catCmd);
  CLI.addCommand("boot", bootCmd);
  CLI.addCommand("cont", contCmd);    
  CLI.addCommand("date", dateCmd);    
  CLI.addCommand("reset", resetCmd);
  CLI.addCommand("trace", traceCmd);  
  CLI.addCommand("patch", patchCmd);
  CLI.addCommand("dump", dumpCmd);
  CLI.addCommand("tftp", tftpCmd);
  CLI.addCommand("?", helpCmd);
  CLI.addCommand("h", helpCmd);
  CLI.addCommand("help", helpCmd);  
}

void loop(bool brk) {
  setup(brk);
  while(active) {
    if (tftpActive) {
      while (SerialESP32.available()) {
        Serial.write(SerialESP32.read());
      }
      int status = WiFi.status();
      if (status != WL_CONNECTED) {    
        //Serial.printf("WiFi status: %d\r\n", status);
        WiFi.end();
        
        Serial.println("\n\rESP32 reset");
        pinMode(SPIWIFI_RST, OUTPUT);        
        digitalWrite(SPIWIFI_RST, LOW);
        delay(10);
        digitalWrite(SPIWIFI_RST, HIGH);
        delay(750);

        while (SerialESP32.available()) {
          Serial.write(SerialESP32.read());
        }
        WiFi.setLEDs(128, 32, 0);
        status = WiFi.begin(ssid, pass);
        if (status != WL_CONNECTED) {
          WiFi.setLEDs(255,0,0);
        } else {
          WiFi.setLEDs(0, 255, 0);
          tftpService.reconnect();
        }
      } else {    
        tftpService.handle();
        yield();
      }
    }
    CLI.process();
  }
  CLI.removeClient(Serial);
  Serial.println();
  // TFTPServer
}

};