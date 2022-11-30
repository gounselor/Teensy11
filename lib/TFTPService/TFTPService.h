#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <TFTP.h>
#include <TFTPConnection.h>

#define MAX_CONNECTIONS 8

namespace tftp {
     class TFTPService {
          public:
     
               TFTPService();
               ~TFTPService();
               uint8_t begin();          
               void end();    
               void reconnect();
               void handle();
               
          private:
               WiFiUDP udp;
               TFTPConnection *tftpConnection[MAX_CONNECTIONS];

               int port;
               bool initialized;          
               char ipbuf[15];

               int newConnection(const char *filename, const IPAddress& remoteIP, const uint16_t remotePort, const uint16_t opcode);
               void sendError(const IPAddress& ipAddress, const uint16_t port, const error err, const char msg[]);
               const char *ip2c_str(uint32_t ip);          
     };
}