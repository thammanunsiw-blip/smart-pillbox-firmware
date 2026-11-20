/*
 * Smart Pillbox – เฟิร์มแวร์ขั้นที่ 1: ต่อ WiFi และ ThingsBoard Cloud
 * ยังไม่ใช้ชิ้นส่วนอื่นบนบอร์ด จึงลองกับ ESP32 ตัวไหนก็ได้
 * ไลบรารีที่ต้องลง: PubSubClient (Nick O'Leary) และ ArduinoJson (Benoit Blanchon) รุ่น 7
 * Arduino IDE (บอร์ดของกลุ่ม N16R8): Board = ESP32S3 Dev Module, USB CDC On Boot = Enabled, Flash Size = 16MB, PSRAM = OPI PSRAM
 * ทดสอบผ่าน Serial Monitor (115200): t = ส่งผลกินแล้ว, m = ส่งผลลืมกินยา, s = ส่งขอความช่วยเหลือ, r = ขอตารางยาใหม่
 * ชื่อข้อมูลตรงกับหน้า API Contract ของโปรเจกต์
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASS, TB_TOKEN (ห้ามอัปขึ้น GitHub)

#define FW_VERSION "0.1.0"

const char *TB_HOST = "mqtt.thingsboard.cloud";   // MQTT ของ ThingsBoard Cloud
const uint16_t TB_PORT = 1883;

const int LED_RED = 15;     // LED1 บนบอร์ด: ติด = ยังต่อ ThingsBoard ไม่ได้
const int LED_GREEN = 16;   // LED2 บนบอร์ด: ติด = ต่อ ThingsBoard แล้ว

const char *MEALS[] = {"morning", "noon", "evening", "bedtime"};
const char *MEAL_TH[] = {"มื้อเช้า", "มื้อกลางวัน", "มื้อเย็น", "ก่อนนอน"};

WiFiClient net;
PubSubClient mqtt(net);
unsigned long lastTry = 0;
int requestId = 0;

/* ---------- เวลา ---------- */

// เวลาปัจจุบันแบบ Unix หน่วยมิลลิวินาที ได้ 0 ถ้ายังไม่ได้เวลาจากอินเทอร์เน็ต
long long nowMs() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (long long)t * 1000LL : 0;
}

/* ---------- ส่งข้อมูลขึ้น ThingsBoard ---------- */

void publish(const char *topic, const char *body) {
  bool ok = mqtt.publish(topic, body);
  Serial.printf("[MQTT] ส่ง %s %s %s\n", topic, body, ok ? "สำเร็จ" : "ไม่สำเร็จ");
}

// ผลการกินยา: ใส่เวลา ts ถ้ามี ถ้าไม่มี ThingsBoard จะใช้เวลาที่ได้รับแทน
void sendDose(const char *meal, const char *dose, int reminders) {
  char body[160];
  long long ts = nowMs();
  if (ts) snprintf(body, sizeof(body), "{\"ts\":%lld,\"values\":{\"meal\":\"%s\",\"dose\":\"%s\",\"reminders\":%d}}", ts, meal, dose, reminders);
  else snprintf(body, sizeof(body), "{\"meal\":\"%s\",\"dose\":\"%s\",\"reminders\":%d}", meal, dose, reminders);
  publish("v1/devices/me/telemetry", body);
}

void sendSos() {
  char body[80];
  long long ts = nowMs();
  if (ts) snprintf(body, sizeof(body), "{\"ts\":%lld,\"values\":{\"sos\":true}}", ts);
  else snprintf(body, sizeof(body), "{\"sos\":true}");
  publish("v1/devices/me/telemetry", body);
}

// ขอค่า shared attribute ที่ตู้ใช้ คำตอบจะมาที่ v1/devices/me/attributes/response/<id>
void requestShared() {
  char topic[48];
  snprintf(topic, sizeof(topic), "v1/devices/me/attributes/request/%d", ++requestId);
  publish(topic, "{\"sharedKeys\":\"schedule,remind_every_min,remind_max,sos_ack\"}");
}

/* ---------- รับข้อมูลจาก ThingsBoard ---------- */

void handleSchedule(JsonVariant value) {
  JsonDocument parsed;
  JsonObject s;
  if (value.is<const char *>()) {   // เผื่อค่าถูกเก็บเป็นข้อความ JSON
    if (deserializeJson(parsed, value.as<const char *>())) { Serial.println("[ตารางยา] รูปแบบไม่ถูกต้อง"); return; }
    s = parsed.as<JsonObject>();
  } else {
    s = value.as<JsonObject>();
  }
  if (s.isNull()) return;
  Serial.println("[ตารางยา] ได้รับตารางยาใหม่");
  for (int i = 0; i < 4; i++) {
    JsonObject m = s[MEALS[i]].as<JsonObject>();
    const char *t = m["time"] | "--:--";
    const char *med = m["med"] | "";
    Serial.printf("  %s %s  %s\n", MEAL_TH[i], t, med[0] ? med : "(ไม่มียา ไม่ต้องเตือน)");
  }
  // บอกเว็บว่าตู้ได้รับตารางยาแล้ว (ส่งเวลาปัจจุบัน ค่าจึงเปลี่ยนทุกครั้ง)
  char body[64];
  long long ts = nowMs();
  snprintf(body, sizeof(body), "{\"schedule_received\":%lld}", ts ? ts : (long long)millis());
  publish("v1/devices/me/attributes", body);
}

void onMessage(char *topic, byte *payload, unsigned int length) {
  Serial.printf("[MQTT] ได้รับข้อความจาก %s\n", topic);
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, (const char *)payload, length);
  if (err) { Serial.printf("[MQTT] อ่านข้อความไม่ได้: %s\n", err.c_str()); return; }
  // ค่าที่ขอตอนเริ่มต้นจะอยู่ใต้ "shared" ส่วนค่าที่เว็บเพิ่งแก้จะส่งมาตรง ๆ
  JsonObject data = doc["shared"].as<JsonObject>();
  if (data.isNull()) data = doc.as<JsonObject>();
  if (data.isNull()) return;
  if (!data["schedule"].isNull()) handleSchedule(data["schedule"]);
  if (!data["remind_every_min"].isNull()) Serial.printf("[ตั้งค่า] เตือนซ้ำทุก %d นาที\n", data["remind_every_min"].as<int>());
  if (!data["remind_max"].isNull()) Serial.printf("[ตั้งค่า] เตือนสูงสุด %d ครั้ง\n", data["remind_max"].as<int>());
  if (!data["sos_ack"].isNull()) Serial.println("[ฉุกเฉิน] ผู้ดูแลกดรับทราบแล้ว");
}

/* ---------- การเชื่อมต่อ ---------- */

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] กำลังต่อ %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  bool blink = false;
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    blink = !blink;
    digitalWrite(LED_RED, blink);   // ไฟแดงกะพริบระหว่างต่อ WiFi
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] ต่อไม่ได้ ตรวจชื่อและรหัส WiFi (ต้องเป็นคลื่น 2.4 GHz)");
    return;
  }
  Serial.printf("[WiFi] ต่อแล้ว IP %s\n", WiFi.localIP().toString().c_str());
  configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");   // ขอเวลาจากอินเทอร์เน็ต (ไทย UTC+7)
}

void connectThingsBoard() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  if (lastTry && millis() - lastTry < 5000) return;   // ลองใหม่ทุก 5 วินาที
  lastTry = millis();
  Serial.printf("[MQTT] กำลังต่อ %s\n", TB_HOST);
  String clientId = String("pillbox-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (!mqtt.connect(clientId.c_str(), TB_TOKEN, nullptr)) {
    // รหัส 5 = token ผิด, ค่าติดลบ = ต่อเซิร์ฟเวอร์ไม่ได้
    Serial.printf("[MQTT] ต่อไม่ได้ (รหัส %d)\n", mqtt.state());
    return;
  }
  Serial.println("[MQTT] ต่อ ThingsBoard แล้ว ตู้จะขึ้นออนไลน์ในหลังบ้าน");
  mqtt.subscribe("v1/devices/me/attributes");
  mqtt.subscribe("v1/devices/me/attributes/response/+");
  publish("v1/devices/me/attributes", "{\"fw_version\":\"" FW_VERSION "\"}");
  requestShared();
}

/* ---------- ทดสอบผ่าน Serial Monitor ---------- */

void serialCommands() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c <= ' ') return;   // ข้าม Enter และช่องว่าง
  if (!mqtt.connected()) { Serial.println("[ทดสอบ] ยังไม่ได้ต่อ ThingsBoard"); return; }
  if (c == 't') sendDose("morning", "taken", 1);
  else if (c == 'm') sendDose("noon", "missed", 3);
  else if (c == 's') sendSos();
  else if (c == 'r') requestShared();
  else Serial.println("[ทดสอบ] ใช้ได้: t = กินแล้ว, m = ลืมกินยา, s = ขอความช่วยเหลือ, r = ขอตารางยาใหม่");
}

void setup() {
  Serial.begin(115200);
  delay(1500);   // รอให้ USB Serial พร้อม
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  Serial.printf("\nSmart Pillbox เฟิร์มแวร์ %s\n", FW_VERSION);
  mqtt.setServer(TB_HOST, TB_PORT);
  mqtt.setCallback(onMessage);
  mqtt.setBufferSize(8192);   // ตารางยาภาษาไทยยาวเกินค่าตั้งต้น 256 ไบต์
  connectWiFi();
}

void loop() {
  connectWiFi();
  connectThingsBoard();
  mqtt.loop();
  digitalWrite(LED_GREEN, mqtt.connected() ? HIGH : LOW);
  digitalWrite(LED_RED, mqtt.connected() ? LOW : HIGH);
  serialCommands();
}
