# ESP32 Cavius Bridge
This project is a bridge between Cavius Wireless smoke and heat detectors and a home automation system using MQTT. It uses an ESP32 to connect to a MQTT server over WiFi, and a HopeRF RFM69CW module to send and receive data from the Cavius detectors. It is compatible with Cavius detectors purchased in 

## Hardware
The following hardware is required, all can be obtained through Aliexpress or EBay

 - [ESP32 DevKit](https://www.aliexpress.com/item/1005002611857804.html)
 - [RFM69CW](https://www.aliexpress.com/item/32887379895.html) (For AU/NZ Cavius you need 915MHz, for EU Cavius you need 868MHz. Other regions TBC)
 - [RF Antenna](https://www.aliexpress.com/item/32805063234.html) (As per frequency above)

## Wiring
The RFM69 module is connected to the ESP32's hardware HSPI as follows:
|ESP32|RFM69CW|
|--|--|
| GPIO14 | SCK |
| GPIO12 | MISO |
| GPIO13 | MOSI |
| GPIO16 | NSS |
| GND | GND |
| 3V3 | 3.3V |

You will also need to solder on an antenna
![Example Photo](https://raw.githubusercontent.com/dasrue/esp_cavi/main/esp_cavi.jpg)
## Software
In order to compile and upload the code, you will need the following

 - [Arduino IDE](https://www.arduino.cc/en/software)
 - [ESP32 for arduino](https://randomnerdtutorials.com/installing-esp32-arduino-ide-2-0/)
 - [PubSubClient Arduino library](https://github.com/knolleary/pubsubclient/releases). To install, download the zip file, in Arduino IDE got to Sketch -> Include Library -> Add .zip library, and find the downloaded zip file.

There are a few configuration options at the top of the file to configure the WiFi and MQTT information. To change the frequency if you are using EU cavius which uses 869.670MHz instead of 926.365MHz change the register setup lines from this:

    rfm69_write_register(0x07, 0xE7); // RegFrfMsb E7
    rfm69_write_register(0x08, 0x97); // RegFrfMid 97
    rfm69_write_register(0x09, 0xF3); // RegFrfLsb F3 - 926.365MHz, E797F3 (register val = 926.365MHz / fstep(61Hz))
    //rfm69_write_register(0x07, 0xD9); // RegFrfMsb D9
    //rfm69_write_register(0x08, 0x6B); // Reg
    //rfm69_write_register(0x09, 0x6F); // RegFrfLsb 6F - 869.670MHz, D96B6F (register val = 869.670MHz / fstep(61Hz))FrfMid 6B
To this:

    //rfm69_write_register(0x07, 0xE7); // RegFrfMsb E7
    //rfm69_write_register(0x08, 0x97); // RegFrfMid 97
    //rfm69_write_register(0x09, 0xF3); // RegFrfLsb F3 - 926.365MHz, E797F3 (register val = 926.365MHz / fstep(61Hz))
    rfm69_write_register(0x07, 0xD9); // RegFrfMsb D9
    rfm69_write_register(0x08, 0x6B); // Reg
    rfm69_write_register(0x09, 0x6F); // RegFrfLsb 6F - 869.670MHz, D96B6F (register val = 869.670MHz / fstep(61Hz))FrfMid 6B


## Thanks
https://github.com/SteveCooling for writing the [Cavius driver for rtl_433](https://github.com/merbanan/rtl_433/blob/master/src/devices/cavius.c). This has been the source for most of the implementation details here
