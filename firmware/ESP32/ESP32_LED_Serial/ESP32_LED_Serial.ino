// Test de la conexión con el STM32 - grabar en la ESP32 DevKit V1 del zócalo
// (en el STM32 grabar Apps/ESP32_Serial.c)
// Lee líneas por Serial2 y maneja el LED interno: "ON" lo prende y "OFF" lo apaga
// Serial2 a 115200 baud, 8N1: RX2 = GPIO16 (viene de PD5 del STM32), TX2 = GPIO17
// Por el USB (monitor serie a 115200) se ve cada línea que llega
// En el IDE de Arduino: placa "DOIT ESP32 DEVKIT V1"

const int LED_INTERNO = 2;  // GPIO2
const int PIN_RX2 = 16;
const int PIN_TX2 = 17;
const unsigned int LARGO_MAX = 8;

String linea;

void setup() {
  pinMode(LED_INTERNO, OUTPUT);
  digitalWrite(LED_INTERNO, LOW);

  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, PIN_RX2, PIN_TX2);
}

void procesar(const String &orden) {
  Serial.println(orden);

  if (orden == "ON") {
    digitalWrite(LED_INTERNO, HIGH);
  } else if (orden == "OFF") {
    digitalWrite(LED_INTERNO, LOW);
  }
}

void loop() {
  while (Serial2.available()) {
    char c = Serial2.read();

    if (c == '\n') {
      procesar(linea);
      linea = "";
    } else if (linea.length() < LARGO_MAX) {
      linea += c;
    }
  }
}
