#include "TFTP.h"
#include "TFTPConnection.h"

const char err_mode[] = "Invalid mode.";
const char err_nspc[] = "Could not create file.";
const char err_read[] = "Could not read from file.";
const char err_file[] = "File not found.";
const char err_write[] = "Could not write to file.";

namespace tftp {

    TFTPConnection::TFTPConnection(const int id, const char *filename, const IPAddress& ipAddress, 
        const uint16_t dstPort, const uint16_t srcPort, const uint16_t opcode) :
        id(id), dstIP(ipAddress), dstPort(dstPort), srcPort(srcPort), opcode(opcode) {

            memcpy(buf, filename, sizeof(buf));
            lastPktMillis = 0;
            lastSize = 0;
            lastBlock = 0;            
            retries = RECV_RETRIES;
            udp.begin(srcPort);
    };

    void TFTPConnection::reconnect() {
        udp.begin(srcPort);
    };

    int TFTPConnection::handle() {
        int packetSize = udp.parsePacket();
        if (!packetSize) {        
            if ((millis() - lastPktMillis) > (RECV_TIMEOUT * 1000)) {
                switch (opcode) {
                    case RRQ: {                    
                        Serial.printf("TFTPConnection: %d: timeout, block: %d, retry %d\r\n", id, lastBlock, RECV_RETRIES + 1 - retries);
                        if (--retries > 0) {
                            lastPktMillis = sendData(udp, dstIP, dstPort, buf, lastBlock, lastSize);
                        } else {
                            if (file.isOpen()) {
                                file.close();
                            }
                            Serial.printf("TFTPConnection: %d, port %d timed out.\r\n", id, srcPort);
                            udp.stop();
                            return 1;
                        }
                        break;
                    }
                    case WRQ: {
                        Serial.printf("TFTPConnection: %d: timeout, block: %d, retry %d\r\n", id, lastBlock, RECV_RETRIES + 1 - retries);
                        if (--retries > 0) {
                            lastPktMillis = sendAck(udp, dstIP, dstPort, lastBlock);
                        } else {
                            if (file.isOpen()) {
                                file.close();
                            }
                            Serial.printf("TFTPConnection: %d, port %d timed out.\r\n", id, srcPort);
                            udp.stop();
                            return 1;
                        }
                        break;
                    }
                    default: {
                        Serial.printf("TFTPConnection: %d timeout handler unexpected opcode: %d\r\n", id, opcode);
                    }
                }
            }
            return 0;
        }
        tftp_message m;
        int n = udp.read((char *) &m, packetSize);
        if (n < packetSize) {
            Serial.printf("TFTPConnection: %d n < packetSize: n: %d, packetSize: %d\r\n", id, n, packetSize);
            return 0;
        }   
        uint16_t opcode = ntohs(m.opcode);
        //Serial.printf("TFTPConnection: %d: handle(): opcode: %d\r\n", cid, opcode);
        switch (opcode) {
        case DATA: {
            uint16_t block_nr = ntohs(m.data.block_number);            
            if (block_nr == lastBlock + 1) {
                retries = RECV_RETRIES;
                int n = file.write(m.data.data, packetSize - 4);
                if (n < packetSize - 4) {
                    sendError(udp, dstIP, dstPort, ERROR, err_write);
                    file.close();
                    udp.stop();
                    return 1;
                }
                lastBlock = block_nr;
                lastPktMillis = sendAck(udp, dstIP, dstPort, block_nr);
                if (packetSize < 516) {
                    Serial.printf("TFTPConnection: %d: EOF, closing\r\n", id);
                    file.close();
                    udp.stop();
                    return 1;
                }
                return 0;
            } 
            break;
        }
        case ACK: {
            uint16_t block_nr = ntohs(m.ack.block_number);
            if (block_nr == lastBlock) {
                retries = RECV_RETRIES;
                int n = file.readBytes(buf, sizeof(buf));
                lastBlock++;
                lastSize = n;
                lastPktMillis = sendData(udp, dstIP, dstPort, buf, lastBlock, n); 
                if (n == 0) {
                    Serial.printf("TFTPConnection: %d: EOF, closing\r\n", id);
                    file.close();
                    udp.stop();
                    return 1;                
                }                   
            }
            return 0;
            break;
        }
        case ERROR: {
            udp.stop();
            return 1;
            break;
        }
        default:
            udp.stop();
            return 1;
            break;
        }
        return 0;
    }

    int TFTPConnection::handle(tftp_message *m) { // preparsed initial message
        uint16_t opcode = ntohs(m->opcode);
        Serial.printf("TFTPConnection: %d: handle(m): opcode: %d\r\n", id, opcode);
        switch (opcode) {
            case RRQ: {
                String modestr = String(strchr((const char *) m->request.filename_and_mode, 0) + 1);
                Serial.printf("TFTPConnection: %d: RRQ: file: %s, mode: %s\r\n", 
                    id, m->request.filename_and_mode, modestr.c_str());        

                if (! (modestr.equalsIgnoreCase("netascii") || modestr.equalsIgnoreCase("octet"))) {
                    Serial.printf("Invalid mode: %s\r\n", modestr.c_str());
                    sendError(udp, dstIP, dstPort, EINVAL, err_mode);
                    udp.stop();
                    return 1;
                }

                if (file.open(buf, O_RDONLY)) {
                    int n = file.readBytes(buf, sizeof(buf));
                    lastBlock = 1;
                    lastSize = n;
                    lastPktMillis = sendData(udp, dstIP, dstPort, buf, lastBlock, n);    
                    return 0;                
                } else {
                    Serial.println("Could not open file.");
                    sendError(udp, dstIP, dstPort, ENOENT, err_file);
                    udp.stop();
                    return 1;
                }
                break;
            }

            case WRQ: {
                String modestr = String(strchr((const char *) m->request.filename_and_mode, 0) + 1);
                Serial.printf("TFTPConnection: %d: WRQ: file: %s, mode: %s\r\n", 
                    id, m->request.filename_and_mode, modestr.c_str());        
                
                if (! (modestr.equalsIgnoreCase("netascii") || modestr.equalsIgnoreCase("octet"))) {
                    Serial.printf("Invalid mode: %s\r\n", modestr.c_str());
                    sendError(udp, dstIP, dstPort, EINVAL, err_mode);
                    udp.stop();
                    return 1;
                }

                if (file.open(buf, O_CREAT | O_WRITE)) {   
                    lastPktMillis = sendAck(udp, dstIP, dstPort, 0);             
                    return 0;
                } else {
                    Serial.println("Could not create file.");
                    sendError(udp, dstIP, dstPort, ENOSPC, err_nspc);
                    udp.stop();
                    return 1;
                }
                break;
            }
        }     
        return 0;   
    }

    uint32_t sendData(WiFiUDP& udp, IPAddress& ip, uint16_t port, const char *buf, const uint16_t block_nr, const uint16_t n) {
        tftp_message m;
        memset(&m, 0, sizeof(m));
        m.data.opcode = htons(DATA);
        m.data.block_number = htons(block_nr);
        memcpy(m.data.data, buf, n);
        if (udp.beginPacket(ip, port)) {
            udp.write((char *) &m.data, n + 4);
            udp.endPacket();
            return millis();            
        }
        return 0;
    }

    int sendAck(WiFiUDP& udp, IPAddress& ip, const uint16_t port, const uint16_t block_nr) {
        tftp_message m;
        m.ack.opcode = htons(ACK);
        m.ack.block_number = htons(block_nr);
        if (udp.beginPacket(ip, port)) {
            udp.write((char *) &m.ack, sizeof(m.ack));
            udp.endPacket();     
            return millis();
        }
        return 0;
    }

    int sendError(WiFiUDP& udp, IPAddress& ip, const uint16_t port, const uint16_t err, const char *msg) {
        tftp_message m;  
        memset(&m, 0, sizeof(m));
        m.error.opcode = htons(ERROR);
        m.error.error_code = htons(err);
        memcpy(m.error.error_string, msg, sizeof(m.error.error_string));
        if (udp.beginPacket(ip, port)) {            
            udp.write((char *) &m.error, sizeof(m.error));
            udp.endPacket();            
            return millis();
        }
        return 0;
    }

};