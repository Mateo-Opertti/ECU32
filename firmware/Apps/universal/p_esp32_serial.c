// ESP32_Serial: el STM32 le manda órdenes por serial al módulo del zócalo
// Cada 500 ms alterna entre "ON\n" y "OFF\n", y la ESP32 prende o apaga su LED interno
// (grabar ESP32/ESP32_LED_Serial/ESP32_LED_Serial.ino en la ESP32)
// LED verde (PF10): copia la orden que se mandó, tiene que ir igual que el LED de la ESP32
// USART2 a 115200 baud, 8N1: TX = PD5 (va a RX2/GPIO16 de la ESP32). No comparte nada con la consola
// PE7 (EN de la ESP32) no se toca: queda en alta impedancia y el pull-up del DevKit la deja andando

#define ESP_BAUD        115200U
#define ESP_PERIODO_MS  500U

static uint32_t esp_t;
static int esp_prendido;
static const char *esp_pendiente;  // lo que falta mandar de la orden actual
static volatile uint32_t esp_tx_count;

static void esp_init(void)
{
    RCC_APB1ENR1 |= RCC_USART2EN;
    (void)RCC_APB1ENR1;

    gpio_af(ESP_PORT, ESP_RX2, 7);
    USART_BRR(USART2_BASE) = (PCLK1_HZ + ESP_BAUD / 2) / ESP_BAUD;
    USART_CR1(USART2_BASE) = CR1_TE | CR1_UE;

    esp_prendido = 0;
    esp_pendiente = 0;
    esp_tx_count = 0;
    esp_t = ticks - ESP_PERIODO_MS;  // la primera orden sale enseguida
}

static void esp_step(void)
{
    if ((ticks - esp_t) >= ESP_PERIODO_MS) {
        esp_t = ticks;
        esp_prendido = !esp_prendido;
        esp_pendiente = esp_prendido ? "ON\n" : "OFF\n";
        esp_tx_count++;
        leds(0, esp_prendido);
    }

    // Un byte por vez, solo cuando el USART está libre
    if (esp_pendiente && *esp_pendiente && (USART_ISR(USART2_BASE) & ISR_TXE)) {
        USART_TDR(USART2_BASE) = (uint8_t)*esp_pendiente++;
    }
}

static void esp_deinit(void)
{
    USART_CR1(USART2_BASE) = 0;
    RCC_APB1ENR1 &= ~RCC_USART2EN;
    gpio_analogico(ESP_PORT, ESP_RX2);
}

static const prueba_t p_esp32 = {
    .nombre = "esp32", .archivo = "ESP32_Serial",
    .descripcion = "USART2 (PD5) manda ON/OFF a la ESP32 cada 500 ms; el verde copia la orden",
    .init = esp_init, .step = esp_step, .deinit = esp_deinit,
    .ok = &esp_tx_count,
};
