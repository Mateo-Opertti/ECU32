// Test de CAN entre dos placas - placa MAESTRO (grabar CAN_Esclavo.c en la otra placa)
// Cada 100 ms manda por FDCAN1 una trama ID 0x100 con 64 bytes: seq, seq+1, seq+2, ...
// El esclavo la verifica y contesta con la trama ID 0x101, con cada byte invertido,
// así se prueban las dos direcciones. El contador seq además maneja las luces del esclavo
// LEDs: el verde (PF10) alterna con cada respuesta correcta del esclavo,
//       el rojo (PF9) se prende 1 s si llega una respuesta mal o falta,
//       y queda fijo si pasan 500 ms sin una buena
// Bus: 500 kbit/s nominal, 2 Mbit/s en la fase de datos (BRS), 64 bytes de datos
// Reloj del FDCAN: cristal HSE 16 MHz (si no arranca, usa PCLK1 = HSI16)
// Hardware: unir CANH1-CANH1, CANL1-CANL1 y GND entre placas, con 120 ohm en cada punta del bus
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
#define RCC_GPIODEN   (1U << 3)
#define RCC_GPIOFEN   (1U << 5)

// GPIO
#define GPIOA_BASE    0x48000000UL
#define GPIOD_BASE    0x48000C00UL
#define GPIOF_BASE    0x48001400UL
#define GPIO_MODER(b)    REG((b) + 0x00)
#define GPIO_OTYPER(b)   REG((b) + 0x04)
#define GPIO_OSPEEDR(b)  REG((b) + 0x08)
#define GPIO_PUPDR(b)    REG((b) + 0x0C)
#define GPIO_ODR(b)      REG((b) + 0x14)
#define GPIO_BSRR(b)     REG((b) + 0x18)
#define GPIO_AFR(b, pin) REG((b) + 0x20 + ((pin) / 8) * 4)

// FDCAN1
#define FDCAN1_BASE   0x40006400UL
#define FDCAN_DBTP    REG(FDCAN1_BASE + 0x00C)
#define FDCAN_CCCR    REG(FDCAN1_BASE + 0x018)
#define FDCAN_NBTP    REG(FDCAN1_BASE + 0x01C)
#define FDCAN_PSR     REG(FDCAN1_BASE + 0x044)
#define FDCAN_TDCR    REG(FDCAN1_BASE + 0x048)
#define FDCAN_RXF0S   REG(FDCAN1_BASE + 0x090)
#define FDCAN_RXF0A   REG(FDCAN1_BASE + 0x094)
#define FDCAN_TXFQS   REG(FDCAN1_BASE + 0x0C4)
#define FDCAN_TXBRP   REG(FDCAN1_BASE + 0x0C8)
#define FDCAN_TXBAR   REG(FDCAN1_BASE + 0x0CC)
#define CCCR_INIT     (1U << 0)
#define CCCR_CCE      (1U << 1)
#define CCCR_FDOE     (1U << 8)
#define CCCR_BRSE     (1U << 9)
#define TXFQS_TFQF    (1U << 21)

// Message RAM (SRAMCAN): 0x350 bytes para FDCAN1, layout fijo en el G4
#define MRAM          0x4000A400UL
#define MRAM_SIZE     0x350UL
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
#define ID_MAESTRO    0x100  // trama que publica el maestro
#define ID_ESCLAVO    0x101  // trama que publica el esclavo
#define CAN_LEN       64
#define TX_PERIOD_MS  100
#define RX_TIMEOUT_MS 500
#define ERROR_VISIBLE_MS 1000  // cuánto queda prendido el rojo después de un error

#define LED_ROJO      9   // PF9
#define LED_VERDE     10  // PF10
#define CAN1_STB      14  // PD14, low = transceiver en modo normal
#define CAN1_RX       11  // PA11, AF9
#define CAN1_TX       12  // PA12, AF9

static volatile uint32_t ticks;

// Para mirar desde el debugger (panel Watch)
volatile uint32_t tx_count;     // tramas 0x100 puestas en cola
volatile uint32_t resp_ok;      // respuestas 0x101 correctas
volatile uint32_t resp_err;     // respuestas que no llegaron o llegaron mal
volatile uint32_t sin_ack;      // veces que la trama anterior seguía sin que nadie la confirme
volatile uint32_t busoff_count; // veces que el nodo se apagó por bus off y se reinició
volatile uint32_t hse_ok;       // 1 si arrancó el cristal de 16 MHz
volatile uint32_t psr;          // estado de protocolo (error passive, bus off)
volatile uint32_t ultimo_lec;   // último error en fase nominal: 1 stuff, 2 form, 3 ACK, 4 bit1, 5 bit0, 6 CRC
volatile uint32_t ultimo_dlec;  // último error en fase de datos (mismos códigos)

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

static void fdcan_init(void)
{
    FDCAN_CCCR |= CCCR_INIT;
    while (!(FDCAN_CCCR & CCCR_INIT));
    FDCAN_CCCR |= CCCR_CCE;

    // La message RAM arranca con basura
    volatile uint32_t *ram = (volatile uint32_t *)MRAM;
    for (uint32_t i = 0; i < MRAM_SIZE / 4; i++) ram[i] = 0;

    FDCAN_CCCR |= CCCR_FDOE | CCCR_BRSE;  // CAN FD con cambio de bitrate

    // Nominal 500 kbit/s: 32 tq por bit (1 + 25 + 6), muestreo al 81%
    FDCAN_NBTP = (5U << 25) | (0U << 16) | (24U << 8) | 5U;
    // Datos 2 Mbit/s: 8 tq por bit (1 + 5 + 2), muestreo al 75%, con compensación de retardo
    FDCAN_DBTP = (1U << 23) | (0U << 16) | (4U << 8) | (1U << 4) | 1U;
    FDCAN_TDCR = 6U << 8;  // TDCO = punto de muestreo en tq

    // Filtros por defecto: acepta todas las tramas y las guarda en RX FIFO0

    FDCAN_CCCR &= ~CCCR_INIT;  // arranca (CCE se limpia solo)
    while (FDCAN_CCCR & CCCR_INIT);
}

// Reinicia el nodo si cayó en bus off y guarda los errores (leer PSR los borra)
static void fdcan_vigilar(void)
{
    if (FDCAN_CCCR & CCCR_INIT) {
        FDCAN_CCCR &= ~CCCR_INIT;
        while (FDCAN_CCCR & CCCR_INIT);
        busoff_count++;
    }

    uint32_t p = FDCAN_PSR;
    uint32_t lec = p & 7U, dlec = (p >> 8) & 7U;
    psr = p;
    if (lec != 0 && lec != 7) ultimo_lec = lec;
    if (dlec != 0 && dlec != 7) ultimo_dlec = dlec;
}

// Devuelve 1 si la trama quedó en cola, 0 si el TX FIFO está lleno
static int fdcan_send(uint32_t id, const uint8_t *data)
{
    if (FDCAN_TXFQS & TXFQS_TFQF) return 0;

    uint32_t idx = (FDCAN_TXFQS >> 16) & 3U;
    volatile uint32_t *e = (volatile uint32_t *)(MRAM + MRAM_TXBUF + idx * MRAM_ELEM);
    e[0] = id << 18;  // ID estándar de 11 bits
    e[1] = ELEM_FDF | ELEM_BRS | (DLC_64 << 16);
    for (uint32_t i = 0; i < CAN_LEN / 4; i++) {
        e[2 + i] = data[4 * i] | (data[4 * i + 1] << 8) |
                   (data[4 * i + 2] << 16) | ((uint32_t)data[4 * i + 3] << 24);
    }
    FDCAN_TXBAR = 1U << idx;
    return 1;
}

// Devuelve -1 si no hay nada, 1 si llegó una trama del esclavo con el formato esperado, 0 si no
static int fdcan_receive(uint8_t *data)
{
    uint32_t s = FDCAN_RXF0S;
    if ((s & 0xFU) == 0) return -1;

    uint32_t idx = (s >> 8) & 3U;
    volatile uint32_t *e = (volatile uint32_t *)(MRAM + MRAM_RXF0 + idx * MRAM_ELEM);
    uint32_t r0 = e[0], r1 = e[1];
    for (uint32_t i = 0; i < CAN_LEN / 4; i++) {
        uint32_t w = e[2 + i];
        data[4 * i]     = (uint8_t)w;
        data[4 * i + 1] = (uint8_t)(w >> 8);
        data[4 * i + 2] = (uint8_t)(w >> 16);
        data[4 * i + 3] = (uint8_t)(w >> 24);
    }
    FDCAN_RXF0A = idx;  // libera el lugar en el FIFO

    return !(r0 & ELEM_XTD) && ((r0 >> 18) & 0x7FFU) == ID_ESCLAVO &&
           (r1 & ELEM_FDF) && (r1 & ELEM_BRS) && ((r1 >> 16) & 0xFU) == DLC_64;
}

// El esclavo devuelve cada byte de la trama invertido
static int respuesta_ok(const uint8_t *data, uint8_t seq)
{
    for (uint32_t i = 0; i < CAN_LEN; i++) {
        if (data[i] != (uint8_t)~(uint8_t)(seq + i)) return 0;
    }
    return 1;
}

int main(void)
{
    // SysTick a 1 ms con el reloj del CPU
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x7;

    RCC_AHB2ENR |= RCC_GPIOAEN | RCC_GPIODEN | RCC_GPIOFEN;
    (void)RCC_AHB2ENR;  // espera a que se habilite el reloj

    gpio_output(GPIOF_BASE, LED_ROJO);
    gpio_output(GPIOF_BASE, LED_VERDE);

    gpio_output(GPIOD_BASE, CAN1_STB);  // STB del TCAN1046 en low: modo normal

    gpio_af(GPIOA_BASE, CAN1_RX, 9);
    gpio_af(GPIOA_BASE, CAN1_TX, 9);

    fdcan_clock_init();
    fdcan_init();

    uint8_t seq = 0;
    uint8_t esperado = 0;
    int esperando = 0;
    int hay_error = 0;
    uint32_t t_tx = ticks;
    uint32_t t_ok = ticks;
    uint32_t t_err = ticks;

    for (;;) {
        fdcan_vigilar();

        if ((ticks - t_tx) >= TX_PERIOD_MS) {
            t_tx = ticks;

            // Si nadie confirmó la anterior no se encola otra: al volver el esclavo
            // sale esa sola y no una tanda de tramas viejas
            if (FDCAN_TXBRP != 0) {
                sin_ack++;
            } else {
                if (esperando) {  // salió, pero el esclavo no contestó a tiempo
                    resp_err++;
                    hay_error = 1;
                    t_err = ticks;
                }

                uint8_t buf[CAN_LEN];
                for (uint32_t i = 0; i < CAN_LEN; i++) buf[i] = (uint8_t)(seq + i);
                if (fdcan_send(ID_MAESTRO, buf)) {
                    esperado = seq;
                    esperando = 1;
                    seq++;
                    tx_count++;
                }
            }
        }

        uint8_t rx[CAN_LEN];
        int r = fdcan_receive(rx);
        if (r >= 0) {
            if (r == 1 && esperando && respuesta_ok(rx, esperado)) {
                resp_ok++;
                t_ok = ticks;
                GPIO_ODR(GPIOF_BASE) ^= 1U << LED_VERDE;
            } else {
                resp_err++;
                hay_error = 1;
                t_err = ticks;
            }
            esperando = 0;
        }

        if (hay_error && (ticks - t_err) > ERROR_VISIBLE_MS) hay_error = 0;

        if (hay_error || (ticks - t_ok) > RX_TIMEOUT_MS) {
            GPIO_BSRR(GPIOF_BASE) = (1U << LED_ROJO) | (1U << (LED_VERDE + 16));  // rojo on, verde off
        } else {
            GPIO_BSRR(GPIOF_BASE) = 1U << (LED_ROJO + 16);  // rojo off
        }
    }
}
