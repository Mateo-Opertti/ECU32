// Sketch de pruebas para la ESP32 DevKit V1 del zócalo de ECU32
// Va con el firmware Pruebas del STM32 (prueba esp_uart, plan ESP-01, ESP-02 y ESP-03)
// Serial2: RX2 = GPIO16 (viene de PD5 del STM32), TX2 = GPIO17 (va a PD6). Arranca a 115200 8N1
// Líneas auxiliares: GPIO4 (D4) <-> PD4 y GPIO18 (D18) <-> PD3. EN viene de PE7
// Por el USB (monitor serie a 115200) se ve cada comando que llega
// En el IDE de Arduino: placa "DOIT ESP32 DEVKIT V1"
//
// Comandos que manda el STM32, una línea cada uno:
//   PING            -> PONG
//   BAUD <n>        -> OK y cambia la velocidad
//   ECO <texto>     -> devuelve la misma línea
//   RX <n>          -> LISTO, recibe n bytes y contesta CRC <8 hex>
//   TX <n>          -> manda n bytes pseudoaleatorios y después CRC <8 hex>
//   PIN <gpio> <v>  -> pone el GPIO como salida en v y contesta OK
//   LEER <gpio>     -> pone el GPIO como entrada y contesta NIVEL <gpio> <v>
//   SUELTA <gpio>   -> pone el GPIO como entrada y contesta OK
//   WIFI ON|OFF     -> transmite por Wi-Fi sin parar (o deja de hacerlo) y contesta OK
// CRC-32 igual al de la unidad CRC del STM32: polinomio 0x04C11DB7, inicial 0xFFFFFFFF, sin reflejar
// Datos pseudoaleatorios: xorshift32 con semilla 0x12345678, igual que en el STM32

#include <WiFi.h>
#include <WiFiUdp.h>

const int PIN_RX2 = 16;
const int PIN_TX2 = 17;
const uint32_t BAUD_INICIAL = 115200;
const uint32_t SEMILLA = 0x12345678;
const uint32_t TIMEOUT_RX_MS = 3000;

String linea;
uint32_t rng;
bool wifi_on = false;
WiFiUDP udp;
uint8_t paquete[1400];

uint8_t rng_byte() {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (uint8_t)rng;
}

uint32_t crc_byte(uint32_t crc, uint8_t b) {
  crc ^= (uint32_t)b << 24;
  for (int i = 0; i < 8; i++) crc = (crc & 0x80000000UL) ? (crc << 1) ^ 0x04C11DB7UL : (crc << 1);
  return crc;
}

void responder(const String &s) {
  Serial2.print(s);
  Serial2.print("\n");
  Serial.println("  -> " + s);
}

void responder_crc(uint32_t crc) {
  char buf[16];
  snprintf(buf, sizeof(buf), "CRC %08lX", (unsigned long)crc);
  responder(buf);
}

// RX <n>: recibe n bytes en binario
void recibir(uint32_t n) {
  responder("LISTO");
  uint32_t crc = 0xFFFFFFFFUL, i = 0, t = millis();
  while (i < n && millis() - t < TIMEOUT_RX_MS) {
    int c = Serial2.read();
    if (c < 0) continue;
    crc = crc_byte(crc, (uint8_t)c);
    i++;
    t = millis();
  }
  Serial.printf("  recibidos %lu de %lu bytes\n", (unsigned long)i, (unsigned long)n);
  responder_crc(crc);
}

// TX <n>: manda n bytes pseudoaleatorios
void transmitir(uint32_t n) {
  uint8_t buf[256];
  uint32_t crc = 0xFFFFFFFFUL;
  rng = SEMILLA;
  while (n) {
    uint32_t k = n < sizeof(buf) ? n : sizeof(buf);
    for (uint32_t j = 0; j < k; j++) {
      buf[j] = rng_byte();
      crc = crc_byte(crc, buf[j]);
    }
    Serial2.write(buf, k);
    n -= k;
  }
  Serial2.flush();
  responder_crc(crc);
}

void procesar(String l) {
  Serial.println("<- " + l);
  if (l == "PING") {
    responder("PONG");
  } else if (l.startsWith("BAUD ")) {
    uint32_t b = l.substring(5).toInt();
    responder("OK");
    Serial2.flush();
    Serial2.updateBaudRate(b);
  } else if (l.startsWith("ECO ")) {
    responder(l);
  } else if (l.startsWith("RX ")) {
    recibir(l.substring(3).toInt());
  } else if (l.startsWith("TX ")) {
    transmitir(l.substring(3).toInt());
  } else if (l.startsWith("PIN ")) {
    int sp = l.indexOf(' ', 4);
    int g = l.substring(4, sp).toInt();
    int v = l.substring(sp + 1).toInt();
    pinMode(g, OUTPUT);
    digitalWrite(g, v ? HIGH : LOW);
    responder("OK");
  } else if (l.startsWith("LEER ")) {
    int g = l.substring(5).toInt();
    pinMode(g, INPUT);
    delay(2);
    responder("NIVEL " + String(g) + " " + String(digitalRead(g)));
  } else if (l.startsWith("SUELTA ")) {
    pinMode(l.substring(7).toInt(), INPUT);
    responder("OK");
  } else if (l == "WIFI ON") {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ECU32-prueba");
    udp.begin(5000);
    wifi_on = true;
    responder("OK");
  } else if (l == "WIFI OFF") {
    wifi_on = false;
    udp.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    responder("OK");
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.setRxBufferSize(16384);
  Serial2.begin(BAUD_INICIAL, SERIAL_8N1, PIN_RX2, PIN_TX2);
  pinMode(4, INPUT);
  pinMode(18, INPUT);
  for (int i = 0; i < (int)sizeof(paquete); i++) paquete[i] = (uint8_t)i;
  delay(100);
  responder("ESP32 LISTO");
}

void loop() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      procesar(linea);
      linea = "";
    } else if (c != '\r' && linea.length() < 120) {
      linea += c;
    }
  }

  // Wi-Fi transmitiendo sin parar: paquetes UDP de broadcast en la red propia
  if (wifi_on) {
    udp.beginPacket(IPAddress(192, 168, 4, 255), 5000);
    udp.write(paquete, sizeof(paquete));
    udp.endPacket();
  }
}
