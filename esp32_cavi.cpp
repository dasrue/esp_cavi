/*  Copyright (c) 2023 Brad L

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <SPI.h>
#include <WiFi.h>
#include <PubSubClient.h>

// RFM69 settings
#define RFM69_SS  16
#define SPI_CLK   1000000  // 1MHz

// User settings
const char * wifi_ssid = "ssid";
const char * wifi_pass = "password";

const char * mqtt_server = "192.168.100.100";
const char * mqtt_user = "user";
const char * mqtt_pass = "password";
const char * mqtt_topic = "sensors/cavius";    // Topic to publish, will be appended with /{netid} and be a json message

const uint32_t cavi_timeout = 2000;    // Timeout, if similar message is received after this timeout it is treated as a new message.
const uint32_t cavi_mincount = 10;     // Minimum number of received messages before MQTT topic is published

// Structure define
struct cavi_message {
  uint32_t dev;
  uint32_t net;
  uint32_t dup_count;
  uint32_t rx_millis;
  uint8_t msg;
};

// Global vars and classes
SPIClass * my_spi = NULL;
WiFiClient espClient;
PubSubClient mqttclient(espClient);
struct cavi_message last_msg;

const uint8_t mxCRC8_table[256] = {
  0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
  157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
  35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
  190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
  70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
  219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
  101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
  248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
  140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
  17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
  175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
  50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
  202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
  87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
  233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
  116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53};

uint8_t calc_crc8(uint8_t* dat, unsigned len) {
  unsigned i;
  uint8_t curr_crc = 0;
  for(i = 0; i < len; i++) {
    curr_crc = mxCRC8_table[dat[i] ^ curr_crc];
  }
  return curr_crc;
}

uint8_t rfm69_read_register(uint8_t addr) {
  uint8_t rx_data = 0;
  if(my_spi) {
    my_spi->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE0));
    digitalWrite(RFM69_SS, 0);
    my_spi->transfer(addr & 0x7F);
    rx_data = my_spi->transfer(0x00);
    digitalWrite(RFM69_SS, 1);
    my_spi->endTransaction();
  }
  return rx_data;
}

void rfm69_write_register(uint8_t addr, uint8_t val) {
  if(my_spi) {
    my_spi->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE0));
    digitalWrite(RFM69_SS, 0);
    my_spi->transfer(addr | 0x80);  // Write command
    my_spi->transfer(val);
    digitalWrite(RFM69_SS, 1);
    my_spi->endTransaction();
  }
}

void rfm69_write_fifo(uint8_t len, uint8_t *vals) {
  uint8_t i;
  if(my_spi) {
    my_spi->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE0));
    digitalWrite(RFM69_SS, 0);
    my_spi->transfer(0x80);  // Write command, register 0 = fifo
    for(i = 0; i < len; i++) {
      my_spi->transfer(vals[i]);
    }
    digitalWrite(RFM69_SS, 1);
    my_spi->endTransaction();
  }
}

void rfm69_read_fifo(uint8_t len, uint8_t *vals) {
  uint8_t i;
  if(my_spi) {
    my_spi->beginTransaction(SPISettings(SPI_CLK, MSBFIRST, SPI_MODE0));
    digitalWrite(RFM69_SS, 0);
    my_spi->transfer(0x00);  // Read command, register 0 = fifo
    for(i = 0; i < len; i++) {
      vals[i] = my_spi->transfer(0x00);
    }
    digitalWrite(RFM69_SS, 1);
    my_spi->endTransaction();
  }
}

int cavi_sendmsg(uint32_t netid, uint32_t devid, uint8_t msg) {
  int i, j;
  uint8_t msgData[11];

  msgData[0] = netid >> 24;
  msgData[1] = netid >> 16;
  msgData[2] = netid >> 8;
  msgData[3] = netid;
  msgData[4] = msg;
  msgData[5] = ~msg;
  msgData[6] = calc_crc8(msgData, 6);
  msgData[7] = devid >> 24;
  msgData[8] = devid >> 16;
  msgData[9] = devid >> 8;
  msgData[10] = devid;
  
  rfm69_write_register(0x01, 0x0C); // RegOpMode - Transmit mode

  for(j = 0; j < 1000; j++) {   // Wait for up to 1 second for tx to be ready. if timeout return -1
    if(rfm69_read_register(0x27) & (1 << 5)) {
      break;
    }
    delay(1);
  }
  if(j == 1000) {
    return -1;
  }


  for(i = 0; i < 210; i++) {  // Send out packet 210 times (matches recording from original cavius
    rfm69_write_fifo(sizeof(msgData), msgData); // Send out the packet
    for(j = 0; j < 1000; j++) {
      if(rfm69_read_register(0x28) & (1 << 3)) {
        break;
      }
      delay(1);
    }
    if(j == 1000) {
      return -2 - i;   // Reutrn -2 - number of packets successfully sent (ie send 100 packets success, return -102)
    }
  }
  rfm69_write_register(0x01, 0x04); // RegOpMode - Standby mode

  return 0;
}

void cavi_enable_rx() {
   rfm69_write_register(0x01, 0x10); // RegOpMode - Receive mode
}

void cavi_disable_rx() {
  rfm69_write_register(0x01, 0x04); // RegOpMode - Standby mode
}

int cavi_try_rxmsg(uint32_t *netid, uint32_t *devid, uint8_t *msg) {
  uint8_t msgData[11];
  
  if(rfm69_read_register(0x28) & (1 << 2)) {  // Check PayloadReady in RegIrqFlags2
    rfm69_read_fifo(11, msgData);

    *netid = msgData[0] << 24 | msgData[1] << 16 | msgData[2] << 8 | msgData[3];
    *msg = msgData[4];
    *devid = msgData[7] << 24 | msgData[8] << 16 | msgData[9] << 8 | msgData[10];

    if((msgData[4] ^ msgData[5] & 0xFF) != 0xFF) {
      return -1;    // msg check failed
    }
    if(msgData[6] != calc_crc8(msgData, 6)) {
      return -2;    // crc check failed
    }

    return 1;
  }  // Not ready
  return 0;
}

const char * cavi_msgtostr(uint8_t msg) {
  // Messages sourced from https://github.com/merbanan/rtl_433/blob/master/src/devices/cavius.c
  // (thanks to https://github.com/SteveCooling)
  switch(msg) {
    case 0x80:
      return "pairing";
    case 0x40:
      return "test";
    case 0x20:
      return "alarm";
    case 0x10:
      return "warning";
    case 0x08:
      return "battery low";
    case 0x04:
      return "mute";
    case 0x02:
      return "unknown 0x02";
    case 0x01:
      return "unknown 0x01";
    default:
      return "unknown";
  }

}

void setup() {
  uint16_t irq_flags;
  
  Serial.begin(115200);
  my_spi = new SPIClass(HSPI);
  my_spi->begin();
  pinMode(RFM69_SS, OUTPUT);
  digitalWrite(RFM69_SS, 1);  // Default SS line high

  mqttclient.setServer(mqtt_server, 1883);
  //mqttclient.setCallback(mqtt_callback);

  // Connect to wifi
  WiFi.begin(wifi_ssid, wifi_pass);
  Serial.print("WiFi connecting to SSID ");
  Serial.println(wifi_ssid);

  // Setup RFM69
  rfm69_write_register(0x02, 0x02); // RegDataModul - Packet Mode, FSK, Gaussian filter BT = 0.5
  rfm69_write_register(0x03, 0x1a); // RegBitrateMsb
  rfm69_write_register(0x04, 0x0b); // RegBitrateLsb - 4800bps (register val = fosc(32MHz)/4800)
  rfm69_write_register(0x05, 0x00); // RegFdevMsb
  rfm69_write_register(0x06, 0x52); // RegFdevLsb - 5kHz (register val = 5kHz / fstep(61Hz))
  rfm69_write_register(0x07, 0xE7); // RegFrfMsb E7
  rfm69_write_register(0x08, 0x97); // RegFrfMid 97
  rfm69_write_register(0x09, 0xF3); // RegFrfLsb F3 - 926.365MHz, E797F3 (register val = 926.365MHz / fstep(61Hz))
  //rfm69_write_register(0x07, 0xD9); // RegFrfMsb D9
  //rfm69_write_register(0x08, 0x6B); // RegFrfMid 6B
  //rfm69_write_register(0x09, 0x6F); // RegFrfLsb 6F - 869.670MHz, D96B6F (register val = 869.670MHz / fstep(61Hz))
  
  // Transmitter registers
  rfm69_write_register(0x11, 0x82); // RegPaLevel - Pa0On, -16dBm - For RFM69CW can only use PA0. RFM69HCW supports all PA?
  rfm69_write_register(0x12, 0x09); // RegPaRamp - 40us, default
  rfm69_write_register(0x13, 0x1A); // RegOcp - Enabled, 95mA, default

  // Receiver registers
  rfm69_write_register(0x18, 0x00); // RegLna - LNA input 50 ohm, AGC on
  //rfm69_write_register(0x19, 0x55); // RegRxBw - DccFreq 010b = 4%, RxBwMant = 10b, RxBwExp = 5, RxBw = 10.4KHz (calculated bandwith of FSK signal is 9.8kHz) 
  rfm69_write_register(0x19, 0x4C); // RegRxBw - DccFreq 010b = 4%, RxBwMant = 01b, RxBwExp = 4, RxBw = 25KHz

  // Packet engine registers
  rfm69_write_register(0x2C, 0x00); // RegPreambleMsb
  rfm69_write_register(0x2D, 0x0C); // RegPreambleLsb - 12 bytes preamble
  rfm69_write_register(0x2E, 0x98); // RegSyncConfig - 4 bytes sync, sync enabled, 0 errors allowed
  rfm69_write_register(0x2F, 'C');  // RegSyncValue1
  rfm69_write_register(0x30, 'a');  // RegSyncValue2
  rfm69_write_register(0x31, 'v');  // RegSyncValue3
  rfm69_write_register(0x32, 'i');  // RegSyncValue4 - sync string is "Cavi"
  rfm69_write_register(0x37, 0x24); // RegPacketConfig1 - Manchester encode, no CRC check, payloadredy if CRC fails
  rfm69_write_register(0x38, 11);   // RegPayloadLength - 11 bytes
  rfm69_write_register(0x3C, 10);   // RegFifoTresh, trigger on fifo level once it reaches 11

  while (WiFi.status() != WL_CONNECTED) {
    delay(10);
  }

  Serial.print("Wifi connected! My IP is ");
  Serial.println(WiFi.localIP());

  cavi_enable_rx();
}


char jsonStr[128];
char topicStr[128];
uint32_t loopcount = 0;
int inChar;
char serialArray[64];
char serialOutArray[64];
int serialArrayIndex = 0;

void loop() {
  int cavi_rx_rtn;
  struct cavi_message curr_msg;
  

  if(!mqttclient.connected()) {
    Serial.println("Attempting MQTT connection...");
    if(mqttclient.connect("esp_cavi", mqtt_user, mqtt_pass)) {
      Serial.println("MQTT connected");
    } else {
      Serial.println("MQTT connection failed");
    }
  }

  if(mqttclient.loop()) {   // Process cavius messages if loop successful
    cavi_rx_rtn = cavi_try_rxmsg(&curr_msg.net, &curr_msg.dev, &curr_msg.msg);
    if(cavi_rx_rtn > 0) {
      curr_msg.rx_millis = millis();
      if(    curr_msg.net == last_msg.net 
          && curr_msg.dev == last_msg.dev 
          && curr_msg.msg == last_msg.msg
          && (last_msg.rx_millis - curr_msg.rx_millis) > cavi_timeout) {    // Message is considered to be same as last if all conditions true. This is needed as cavius sends message repeatedly
        last_msg.rx_millis = curr_msg.rx_millis;
        last_msg.dup_count++;
        if(last_msg.dup_count == cavi_mincount) {   // Same message has been repeatedly sent cavi_mincount times. Only send it once.
          snprintf(jsonStr, sizeof(jsonStr) - 1, "{\"device\":%u,\"message\":\"%s\"}", last_msg.dev, cavi_msgtostr(last_msg.msg));
          snprintf(topicStr, sizeof(topicStr) - 1, "%s/%u", mqtt_topic, last_msg.net);
          mqttclient.publish(topicStr, jsonStr);   // TODO construct JSON string and publish it
        }
      } else {  // New message
        memcpy(&last_msg, &curr_msg, sizeof(struct cavi_message));
        last_msg.dup_count = 1;
      }
    }    
  }
  
  if (Serial.available() > 0) {
    inChar = Serial.read();
    if(inChar == '\n') {
      Serial.println(" ");
      cavi_disable_rx();
      if(strcmp("testmine",serialArray) == 0) {
        Serial.println("Sending test alarm to 212982 group");
        if(cavi_sendmsg(212982, 8989, 0x40) == 0) {
          Serial.println("Sent");
        } else {
          Serial.println("Send failed!");
        }
      } else if(strcmp("test",serialArray) == 0) {
        Serial.println("Sending test alarm");
        if(cavi_sendmsg(123456, 8989, 0x40) == 0) {
          Serial.println("Sent");
        } else {
          Serial.println("Send failed!");
        }
      } else if(strcmp("mute",serialArray) == 0) {
        Serial.println("Sending mute");
        if(cavi_sendmsg(123456, 8989, 0x04) == 0) {
          Serial.println("Sent");
        } else {
          Serial.println("Send failed!");
        }
      }

      serialArrayIndex = 0;
      cavi_enable_rx();
    } else {
      if((serialArrayIndex < sizeof(serialArray))) {
        Serial.print(inChar);
        serialArray[serialArrayIndex++] = inChar;
      }
    }
    
  }
  delay(1);
}
