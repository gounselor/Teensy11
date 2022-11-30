#pragma once

#include <Arduino.h>

#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include "SdFat.h"
#include "sdios.h"

namespace tftp {
    const int RECV_TIMEOUT = 1;
    const int RECV_RETRIES = 5;

    class TFTPConnection {
        public:

        TFTPConnection(const int id, const char *filename, const IPAddress& remoteIP, 
            const uint16_t remotePort, const uint16_t localPort, const uint16_t opcode);
        void reconnect();
        int handle();
        int handle(tftp_message *m);
                
        private:

        int id;
        char buf[512];
        FsFile file;
        WiFiUDP udp;
        IPAddress dstIP;        
        uint32_t lastPktMillis;
        uint16_t dstPort;      
        uint16_t srcPort;                
        uint16_t lastBlock;
        uint16_t lastSize;
        uint16_t retries;          
        uint16_t opcode;
    };

    uint32_t sendData(WiFiUDP& udp, IPAddress& ip, uint16_t port, const char *buf, const uint16_t block_nr, const uint16_t n);
    int sendAck(WiFiUDP& udp, IPAddress& ip, const uint16_t port, const uint16_t block_nr);    
    int sendError(WiFiUDP& udp, IPAddress& ip, const uint16_t port, const uint16_t err, const char *msg);
};