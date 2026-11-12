// Test de FDCAN: FDCAN1 manda una trama CAN FD cada 100 ms y FDCAN2 la recibe
// - Cada trama recibida bien alterna el LED verde (PF10): si parpadea, funciona
// - El LED rojo (PF9) se prende si llega una trama mal o si pasan 500 ms sin recibir
// Bus: 500 kbit/s nominal, 2 Mbit/s en la fase de datos (BRS), 64 bytes de datos
// Reloj del FDCAN: cristal HSE 16 MHz (si no arranca, usa PCLK1 = HSI16)
// Hardware: unir CANH1-CANH2 y CANL1-CANL2, con terminación de 120 ohm
// STM32G491VC (reloj del CPU por defecto: HSI16 = 16 MHz)
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// RCC (RM0440)
#define RCC_BASE      0x40021000UL
#define RCC_CR        REG(RCC_BASE + 0x00)
#define RCC_AHB2ENR   REG(RCC_BASE + 0x4C)
#define RCC_APB1ENR1  REG(RCC_BASE + 0x58)
#define RCC_CCIPR     REG(RCC_BASE + 0x88)
#define RCC_HSEON     (1U << 16)
#define RCC_HSERDY    (1U << 17)
#define RCC_FDCANEN   (1U << 25)
#define RCC_GPIOAEN   (1U << 0)
#define RCC_GPIOBEN   (1U << 1)
#define RCC_GPIODEN   (1U << 3)
#define RCC_GPIOFEN   (1U << 5)

// GPIO
#define GPIOA_BASE    0x48000000UL
#define GPIOB_BASE    0x48000400UL
#define GPIOD_BASE    0x48000C00UL
#define GPIOF_BASE    0x48001400UL
#define GPIO_MODER(b)    REG((b) + 0x00)
#define GPIO_OTYPER(b)   REG((b) + 0x04)
#define GPIO_OSPEEDR(b)  REG((b) + 0x08)
#define GPIO_PUPDR(b)    REG((b) + 0x0C)
#define GPIO_ODR(b)      REG((b) + 0x14)
#define GPIO_BSRR(b)     REG((b) + 0x18)
#define GPIO_AFR(b, pin) REG((b) + 0x20 + ((pin) / 8) * 4)

// FDCAN
#define FDCAN1_BASE   0x40006400UL
#define FDCAN2_BASE   0x40006800UL
#define FDCAN_DBTP(b)   REG((b) + 0x00C)
#define FDCAN_CCCR(b)   REG((b) + 0x018)
#define FDCAN_NBTP(b)   REG((b) + 0x01C)
#define FDCAN_PSR(b)    REG((b) + 0x044)
#define FDCAN_TDCR(b)   REG((b) + 0x048)
#define FDCAN_RXF0S(b)  REG((b) + 0x090)
#define FDCAN_RXF0A(b)  REG((b) + 0x094)
#define FDCAN_TXFQS(b)  REG((b) + 0x0C4)
#define FDCAN_TXBAR(b)  REG((b) + 0x0CC)
#define CCCR_INIT     (1U << 0)
#define CCCR_CCE      (1U << 1)
#define CCCR_FDOE     (1U << 8)
#define CCCR_BRSE     (1U << 9)
#define TXFQS_TFQF    (1U << 21)

// Message RAM (SRAMCAN): 0x350 bytes por instancia, layout fijo en el G4
#define SRAMCAN_BASE  0x4000A400UL
#define SRAMCAN_SIZE  0x350UL
#define MRAM(b)       (SRAMCAN_BASE + ((b) == FDCAN1_BASE ? 0 : SRAMCAN_SIZE))
#define MRAM_RXF0     0x0B0UL
#define MRAM_TXBUF    0x278UL
#define MRAM_ELEM     72UL  // 2 words de header + 64 bytes de datos

// Header de las tramas
#define ELEM_XTD      (1U << 30)
#define ELEM_FDF      (1U << 21)
#define ELEM_BRS      (1U << 20)
#define DLC_64        15U

// SysTick y FPU (Cortex-M4)
#define SYST_CSR      REG(0xE000E010UL)
#define SYST_RVR      REG(0xE000E014UL)
#define SYST_CVR      REG(0xE000E018UL)
#define SCB_CPACR     REG(0xE000ED88UL)

#define CPU_HZ        16000000U
#define TX_PERIOD_MS  100
#define RX_TIMEOUT_MS 500
#define CAN_ID        0x123
#define CAN_LEN       64

#define LED_ROJO      9   // PF9
#define LED_VERDE     10  // PF10
#define CAN1_STB      14  // PD14, low = transceiver en modo normal
#define CAN2_STB      15  // PD15
#define CAN1_RX       11  // PA11, AF9
#define CAN1_TX       12  // PA12, AF9
#define CAN2_RX       5   // PB5, AF9
#define CAN2_TX       6   // PB6, AF9

static volatile uint32_t ticks;

// Para mirar desde el debugger (panel Watch)
volatile uint32_t tx_count;   // tramas que mandó FDCAN1
volatile uint32_t rx_ok;      // tramas que FDCAN2 recibió bien
volatile uint32_t rx_err;     // tramas que llegaron con algo mal
volatile uint32_t hse_ok;     // 1 si arrancó el cristal de 16 MHz
volatile uint32_t psr1, psr2; // estado de protocolo (LEC, error passive, bus off)

// La llama el startup antes de main
void SystemInit(void)
{
    SCB_CPACR |= (0xFU << 20);  // habilita la FPU
}

void SysTick_Handler(void) { ticks++; }

static void gpio_output(uint32_t base, uint32_t pin)
{
    GPIO_BSRR(base) = 1U << (pin + 16);  // low antes de pasarlo a salida
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (1U << (pin * 2));
    GPIO_OTYPER(base) &= ~(1U << pin);
    GPIO_PUPDR(base) &= ~(3U << (pin * 2));
}

static void gpio_af(uint32_t base, uint32_t pin, uint32_t af)
{
    uint32_t sh = (pin % 8) * 4;
    GPIO_AFR(base, pin) = (GPIO_AFR(base, pin) & ~(0xFU << sh)) | (af << sh);
    GPIO_OSPEEDR(base) |= 2U << (pin * 2);  // velocidad alta
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (2U << (pin * 2));
}

static void fdcan_clock_init(void)
{
    RCC_CR |= RCC_HSEON;
    uint32_t t = 100000;
    while (!(RCC_CR & RCC_HSERDY) && --t);
    hse_ok = (t != 0);

    // FDCANSEL: 00 = HSE, 10 = PCLK1. Las dos dan 16 MHz, así que los tiempos de bit no cambian
    RCC_CCIPR = (RCC_CCIPR & ~(3U << 24)) | ((hse_ok ? 0U : 2U) << 24);

    RCC_APB1ENR1 |= RCC_FDCANEN;
    (void)RCC_APB1ENR1;
}

static void fdcan_init(uint32_t base)
{
    FDCAN_CCCR(base) |= CCCR_INIT;
    while (!(FDCAN_CCCR(base) & CCCR_INIT));
    FDCAN_CCCR(base) |= CCCR_CCE;

    // La message RAM arranca con basura
    volatile uint32_t *ram = (volatile uint32_t *)MRAM(base);
    for (uint32_t i = 0; i < SRAMCAN_SIZE / 4; i++) ram[i] = 0;

    FDCAN_CCCR(base) |= CCCR_FDOE | CCCR_BRSE;  // CAN FD con cambio de bitrate

    // Nominal 500 kbit/s: 32 tq por bit (1 + 25 + 6), muestreo al 81%
    FDCAN_NBTP(base) = (5U << 25) | (0U << 16) | (24U << 8) | 5U;
    // Datos 2 Mbit/s: 8 tq por bit (1 + 5 + 2), muestreo al 75%, con compensación de retardo
    FDCAN_DBTP(base) = (1U << 23) | (0U << 16) | (4U << 8) | (1U << 4) | 1U;
    FDCAN_TDCR(base) = 6U << 8;  // TDCO = punto de muestreo en tq

    // Filtros por defecto: acepta todas las tramas y las guarda en RX FIFO0

    FDCAN_CCCR(base) &= ~CCCR_INIT;  // arranca (CCE se limpia solo)
    while (FDCAN_CCCR(base) & CCCR_INIT);
}

// Devuelve 1 si la trama quedó en cola, 0 si el TX FIFO está lleno
static int fdcan_send(uint32_t base, uint32_t id, const uint8_t *data)
{
    if (FDCAN_TXFQS(base) & TXFQS_TFQF) return 0;

    uint32_t idx = (FDCAN_TXFQS(base) >> 16) & 3U;
    volatile uint32_t *e = (volatile uint32_t *)(MRAM(base) + MRAM_TXBUF + idx * MRAM_ELEM);
    e[0] = id << 18;  // ID estándar de 11 bits
    e[1] = ELEM_FDF | ELEM_BRS | (DLC_64 << 16);
    for (uint32_t i = 0; i < CAN_LEN / 4; i++) {
        e[2 + i] = data[4 * i] | (data[4 * i + 1] << 8) |
                   (data[4 * i + 2] << 16) | ((uint32_t)data[4 * i + 3] << 24);
    }
    FDCAN_TXBAR(base) = 1U << idx;
    return 1;
}

// Devuelve -1 si no hay nada, 1 si la trama es la esperada, 0 si llegó algo distinto
static int fdcan_receive(uint32_t base)
{
    uint32_t s = FDCAN_RXF0S(base);
    if ((s & 0xFU) == 0) return -1;

    uint32_t idx = (s >> 8) & 3U;
    volatile uint32_t *e = (volatile uint32_t *)(MRAM(base) + MRAM_RXF0 + idx * MRAM_ELEM);
    uint32_t r0 = e[0], r1 = e[1];
    uint8_t data[CAN_LEN];
    for (uint32_t i = 0; i < CAN_LEN / 4; i++) {
        uint32_t w = e[2 + i];
        data[4 * i]     = (uint8_t)w;
        data[4 * i + 1] = (uint8_t)(w >> 8);
        data[4 * i + 2] = (uint8_t)(w >> 16);
        data[4 * i + 3] = (uint8_t)(w >> 24);
    }
    FDCAN_RXF0A(base) = idx;  // libera el lugar en el FIFO

    int ok = !(r0 & ELEM_XTD) && ((r0 >> 18) & 0x7FFU) == CAN_ID &&
             (r1 & ELEM_FDF) && (r1 & ELEM_BRS) && ((r1 >> 16) & 0xFU) == DLC_64;
    for (uint32_t i = 1; i < CAN_LEN; i++) {
        if (data[i] != (uint8_t)(data[0] + i)) ok = 0;
    }
    return ok;
}

int main(void)
{
    // SysTick a 1 ms con el reloj del CPU
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x7;

    RCC_AHB2ENR |= RCC_GPIOAEN | RCC_GPIOBEN | RCC_GPIODEN | RCC_GPIOFEN;
    (void)RCC_AHB2ENR;  // espera a que se habilite el reloj

    gpio_output(GPIOF_BASE, LED_ROJO);
    gpio_output(GPIOF_BASE, LED_VERDE);

    // STB de los TCAN1046 en low: transceivers en modo normal
    gpio_output(GPIOD_BASE, CAN1_STB);
    gpio_output(GPIOD_BASE, CAN2_STB);

    gpio_af(GPIOA_BASE, CAN1_RX, 9);
    gpio_af(GPIOA_BASE, CAN1_TX, 9);
    gpio_af(GPIOB_BASE, CAN2_RX, 9);
    gpio_af(GPIOB_BASE, CAN2_TX, 9);

    fdcan_clock_init();
    fdcan_init(FDCAN1_BASE);
    fdcan_init(FDCAN2_BASE);

    uint8_t seq = 0;
    uint32_t t_tx = ticks;
    uint32_t t_ok = ticks;
    int error = 0;

    for (;;) {
        // FDCAN1: manda 64 bytes que arrancan en seq y van subiendo de a 1
        if ((ticks - t_tx) >= TX_PERIOD_MS) {
            t_tx = ticks;
            uint8_t buf[CAN_LEN];
            for (uint32_t i = 0; i < CAN_LEN; i++) buf[i] = (uint8_t)(seq + i);
            if (fdcan_send(FDCAN1_BASE, CAN_ID, buf)) {
                seq++;
                tx_count++;
            }
        }

        // FDCAN2: verifica lo que llega
        int r = fdcan_receive(FDCAN2_BASE);
        if (r == 1) {
            rx_ok++;
            t_ok = ticks;
            error = 0;
            GPIO_ODR(GPIOF_BASE) ^= 1U << LED_VERDE;
        } else if (r == 0) {
            rx_err++;
            error = 1;
        }

        if (error || (ticks - t_ok) > RX_TIMEOUT_MS) {
            GPIO_BSRR(GPIOF_BASE) = (1U << LED_ROJO) | (1U << (LED_VERDE + 16));  // rojo on, verde off
        } else {
            GPIO_BSRR(GPIOF_BASE) = 1U << (LED_ROJO + 16);  // rojo off
        }

        psr1 = FDCAN_PSR(FDCAN1_BASE);
        psr2 = FDCAN_PSR(FDCAN2_BASE);
    }
}
