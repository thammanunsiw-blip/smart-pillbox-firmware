# Smart Pillbox – เฟิร์มแวร์ตู้จ่ายยา

เฟิร์มแวร์ของตู้จ่ายยาอัตโนมัติสำหรับผู้สูงอายุ ใช้ ESP32-S3 บนแผงวงจรที่กลุ่มออกแบบเอง ตู้ส่งผลการกินยาและการขอความช่วยเหลือขึ้น ThingsBoard Cloud ผ่าน MQTT และรับตารางยาที่ผู้ดูแลตั้งจากเว็บไซต์

- เว็บผู้ดูแล: https://nattakornsae-byte.github.io/smart-pillbox-app/
- ระบบหลังบ้าน: https://nattakornsae-byte.github.io/Smart-Box-Admin/

## สถานะ

| ขั้น | เนื้อหา | สถานะ |
|---|---|---|
| 1 (v0.1.0) | ต่อ WiFi และ ThingsBoard, รับตารางยา, ทดสอบส่งผลผ่าน Serial Monitor | เสร็จ |
| 2 | ทำงานตามตารางยา: ปุ่ม ฝา เซอร์โว บัซเซอร์ LED จอ นาฬิกา DS3231 การ์ด SD และรับคำสั่งจากแดชบอร์ด ThingsBoard | กำลังทำ |

## สิ่งที่ต้องใช้

- Arduino IDE 2 และบอร์ด **esp32 by Espressif Systems** (Boards Manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`)
- ไลบรารี: **PubSubClient** (Nick O'Leary), **ArduinoJson** รุ่น 7 (Benoit Blanchon)

## ตั้งค่าและอัปโหลด

1. ดาวน์โหลด repo นี้ แล้วเปิด `smart_pillbox.ino` (ถ้า Arduino IDE ถามให้สร้างโฟลเดอร์ `smart_pillbox` ให้กด OK)
2. คัดลอก `secrets.example.h` เป็น `secrets.h` ไว้ในโฟลเดอร์เดียวกัน แล้วกรอกชื่อและรหัส WiFi (2.4 GHz) กับ Access Token ของตู้ · **ห้ามอัป `secrets.h` ขึ้น GitHub**
3. เมนู Tools: Board = ESP32S3 Dev Module · USB CDC On Boot = Enabled · Flash Size = 16MB · PSRAM = OPI PSRAM
4. กด Upload ถ้าไม่ผ่าน ให้กด BOOT ค้าง กด RESET 1 ครั้ง ปล่อย BOOT แล้ว Upload ใหม่
5. เปิด Serial Monitor ที่ 115200 · พิมพ์ `t` = ส่งผลกินแล้ว, `m` = ส่งผลลืมกินยา, `s` = ขอความช่วยเหลือ, `r` = ขอตารางยาใหม่

## ขาที่ใช้ (ESP32-S3-WROOM-1 N16R8)

| ขา | อุปกรณ์ | หมายเหตุ |
|---|---|---|
| GPIO4 | บัซเซอร์ active 2.3 kHz ผ่านทรานซิสเตอร์ Q1 | HIGH = ดัง |
| GPIO5 | เซอร์โว SG90 (PWM 50 Hz) | หมุนช่องยาของมื้อนั้นมาที่ช่องหยิบ |
| GPIO6 | ลิมิตสวิตช์ฝากล่อง | ฝาปิด = LOW |
| GPIO7 | ปุ่ม SW และปุ่มใหญ่ H3 | กด = LOW · กดสั้น = กินยาแล้ว · กดค้าง 3 วินาที = ขอความช่วยเหลือ |
| GPIO8 / GPIO9 | I2C SDA / SCL | นาฬิกา DS3231 (0x68) และจอ OLED (0x3C) |
| GPIO10–13 | SPI CS / MOSI / SCK / MISO | การ์ด microSD |
| GPIO15 / GPIO16 | LED แดง / LED เขียว | แดง = ยังต่อ ThingsBoard ไม่ได้ · เขียว = ต่อแล้ว |
| GPIO19 / GPIO20 | USB D− / D+ | อัปโหลดโปรแกรมและ Serial |

## ข้อมูลที่รับส่งกับ ThingsBoard (MQTT, mqtt.thingsboard.cloud:1883)

| ทิศทาง | Topic | ข้อมูล |
|---|---|---|
| ตู้ → ThingsBoard | `v1/devices/me/telemetry` | `meal` (morning, noon, evening, bedtime), `dose` (taken, missed), `reminders` (1–3), `sos` (true) |
| ตู้ → ThingsBoard | `v1/devices/me/attributes` | `fw_version`, `schedule_received` |
| ThingsBoard → ตู้ | `v1/devices/me/attributes` | `schedule`, `remind_every_min`, `remind_max`, `sos_ack` |

ThingsBoard ส่งต่อผลกินยา การลืมกินยา และการขอความช่วยเหลือ ไปที่เว็บไซต์ผู้ดูแลและ LINE ของผู้ดูแล
