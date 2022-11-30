#include <Arduino.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include "TFTP.h"
#include "TFTPService.h"

#include <string.h>

using namespace tftp;

const char err_conn[] = "No connection available.";

TFTPService::TFTPService() : port (69) {}

TFTPService::~TFTPService() {
    udp.stop();
}

uint8_t TFTPService::begin() {
    uint8_t res = udp.begin(port);
    if (res) {
        initialized = true;
    }
    return res;
}

void TFTPService::end() {
    udp.stop();
}

void TFTPService::reconnect() {
    udp.begin(port);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (tftpConnection[i] != NULL) {
            tftpConnection[i]->reconnect();
        }        
    }
}

void TFTPService::handle() {
    if (!initialized) {
        return;
    }
    
    for (int i = 0; i < MAX_CONNECTIONS; i++) {    
        if (tftpConnection[i] != NULL) {
            int done = tftpConnection[i]->handle();
            if (done) {
                Serial.printf("TFTPService: removing connection %d\r\n", i);
                delete(tftpConnection[i]);
                tftpConnection[i] = NULL;                
            }            
            delay(20);            
        }
    }        

    int packetSize = udp.parsePacket();
    if (!packetSize) {
        return;
    }
    tftp_message m;
    int n = udp.read((char *) &m, packetSize);
    if (n < packetSize) {
        Serial.printf("TFTPService: recv: short packet size: %d < %d\r\n", n, packetSize);
        return;
    }
    uint16_t opcode = ntohs(m.opcode);
    Serial.printf("TFTPService: connection: %s:%d\r\n", ip2c_str(udp.remoteIP()), udp.remotePort());   
    
    switch (opcode) {
        case RRQ: {
            int id = newConnection((char *) m.request.filename_and_mode, udp.remoteIP(), udp.remotePort(), opcode);
            if (id == -1) {
                sendError(udp.remoteIP(), udp.remotePort(), UNDEF, err_conn);
                return;
            }          
            Serial.printf("TFTPService: connection: %d\r\n", id);
            if (tftpConnection[id]->handle(&m)) {
                Serial.printf("Removing connection %d\r\n", id);
                free(tftpConnection[id]);
                tftpConnection[id] = NULL;
            }
            break;
        }
        case WRQ: {
            int id = newConnection((char *) m.request.filename_and_mode, udp.remoteIP(), udp.remotePort(), opcode);
            if (id == -1) {
                sendError(udp.remoteIP(), udp.remotePort(), UNDEF, err_conn);
                return;
            }
            Serial.printf("TFTPService: connection: %d\r\n", id);
            if (tftpConnection[id]->handle(&m)) {
                Serial.printf("Removing connection %d\r\n", id);
                free(tftpConnection[id]);
                tftpConnection[id] = NULL;
            }
            break;
        }
        default: {
            Serial.printf("Unexpected opcode %d from %s:%d\r\n", opcode, udp.remoteIP(), udp.remotePort());
            return;
        }
    }
}

int TFTPService::newConnection(const char *filename, const IPAddress& remoteIP, const uint16_t remotePort, const uint16_t opcode) {
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (tftpConnection[i] == NULL) {
            uint16_t localPort = 6900 + i;
            tftpConnection[i] = new TFTPConnection(i, filename, remoteIP, remotePort, localPort, opcode);            
            return i;
        }
    }
    return -1;
}

void TFTPService::sendError(const IPAddress& ip, const u_int16_t port, const error err, const char msg[]) {    
    tftp_message m;
    memset(&m, 0, sizeof(m));
    m.error.opcode = htons(ERROR);
    m.error.error_code = htons(err);
    memcpy(m.error.error_string, msg, sizeof(m.error.error_string));
    Serial.printf("ERROR: err: %s, tid: %d\r\n", err, port);
    WiFiUDP udp;
    udp.beginPacket(ip, port);
    udp.write((char *) &m.error);
    udp.endPacket();
}   
    
const char *TFTPService::ip2c_str(uint32_t ip) {
    uint32_t ip_addr = ntohl(ip);
    snprintf(ipbuf, sizeof(ipbuf), "%d.%d.%d.%d", 
    (uint8_t) ((ip_addr >> 24) & 255),
    (uint8_t) ((ip_addr >> 16) & 255),
    (uint8_t) ((ip_addr >> 8) & 255),
    (uint8_t) ((ip_addr) & 255));
    return ipbuf;
}
