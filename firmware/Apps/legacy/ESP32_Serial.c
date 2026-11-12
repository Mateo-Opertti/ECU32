// Test de la conexión con la ESP32: el STM32 le manda órdenes por serial al módulo del zócalo
// Cada 500 ms alterna entre "ON\n" y "OFF\n", y la ESP32 prende o apaga su LED interno
// (grabar ESP32/ESP32_LED_Serial/ESP32_LED_Serial.ino en la ESP32)
// LED verde (PF10): copia la orden que se mandó, tiene que ir igual que el LED de la ESP32
// USART2 a 115200 baud, 8N1: TX = PD5 (va a RX2/GPIO16 de la ESP32)
// PE7 (EN de la ESP32) no se toca: queda en alta impedancia y el pull-up del DevKit la deja andando
// STM32G491VC (reloj por defecto: HSI16 = 16 MHz)
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// RCC (RM0440)
#define RCC_AHB2ENR   REG(0x4002104CUL)
#define RCC_APB1ENR1  REG(0x40021058UL)
#define RCC_GPIODEN   (1U << 3)
#define RCC_GPIOFEN   (1U << 5)
#define RCC_USART2EN  (1U << 17)

// GPIO
#define GPIOD_BASE    0x48000C00UL
#define GPIOF_BASE    0x48001400UL
#define GPIO_MODER(b)    REG((b) + 0x00)
#define GPIO_OTYPER(b)   REG((b) + 0x04)
#define GPIO_PUPDR(b)    REG((b) + 0x0C)
#define GPIO_BSRR(b)     REG((b) + 0x18)
#define GPIO_AFR(b, pin) REG((b) + 0x20 + ((pin) / 8) * 4)

// USART2 (reloj por defecto: PCLK1 = 16 MHz)
#define USART2_BASE   0x40004400UL
#define USART2_CR1    REG(USART2_BASE + 0x00)
#define USART2_BRR    REG(USART2_BASE + 0x0C)
#define USART2_ISR    REG(USART2_BASE + 0x1C)
#define USART2_TDR    REG(USART2_BASE + 0x28)
#define CR1_UE        (1U << 0)
#define CR1_TE        (1U << 3)
#define ISR_TXE       (1U << 7)

// SysTick y FPU (Cortex-M4)
#define SYST_CSR      REG(0xE000E010UL)
#define SYST_RVR      REG(0xE000E014UL)
#define SYST_CVR      REG(0xE000E018UL)
#define SCB_CPACR     REG(0xE000ED88UL)

#define CPU_HZ        16000000U
#define UART_BAUD     115200U
#define PERIODO_MS    500

#define LED_VERDE     10  // PF10
#define ESP_RX2       5   // PD5, AF7 (USART2_TX)

static volatile uint32_t ticks;

// Para mirar desde el debugger (panel Watch)
volatile uint32_t tx_count;   // órdenes enviadas

// La llama el startup antes de main
void SystemInit(void)
{
    SCB_CPACR |= (0xFU << 20);  // habilita la FPU
}

void SysTick_Handler(void) { ticks++; }

static void delay_ms(uint32_t ms)
{
    uint32_t t0 = ticks;
    while ((ticks - t0) < ms);
}

static void gpio_output(uint32_t base, uint32_t pin)
{
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (1U << (pin * 2));
    GPIO_OTYPER(base) &= ~(1U << pin);
    GPIO_PUPDR(base) &= ~(3U << (pin * 2));
}

static void gpio_af(uint32_t base, uint32_t pin, uint32_t af)
{
    uint32_t sh = (pin % 8) * 4;
    GPIO_AFR(base, pin) = (GPIO_AFR(base, pin) & ~(0xFU << sh)) | (af << sh);
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (2U << (pin * 2));
}

static void uart_puts(const char *s)
{
    while (*s) {
        while (!(USART2_ISR & ISR_TXE));
        USART2_TDR = (uint8_t)*s++;
    }
}

int main(void)
{
    // SysTick a 1 ms con el reloj del CPU
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x7;

    RCC_AHB2ENR |= RCC_GPIODEN | RCC_GPIOFEN;
    RCC_APB1ENR1 |= RCC_USART2EN;
    (void)RCC_APB1ENR1;  // espera a que se habilite el reloj

    gpio_output(GPIOF_BASE, LED_VERDE);
    gpio_af(GPIOD_BASE, ESP_RX2, 7);

    USART2_BRR = (CPU_HZ + UART_BAUD / 2) / UART_BAUD;
    USART2_CR1 = CR1_TE | CR1_UE;

    int prendido = 0;

    for (;;) {
        prendido = !prendido;
        uart_puts(prendido ? "ON\n" : "OFF\n");
        tx_count++;
        GPIO_BSRR(GPIOF_BASE) = 1U << (prendido ? LED_VERDE : LED_VERDE + 16);
        delay_ms(PERIODO_MS);
    }
}
