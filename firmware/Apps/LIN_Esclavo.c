// Test de LIN - placa ESCLAVO (grabar LIN_Maestro.c en la otra placa)
// - ID 0x10: recibe los 4 bytes del maestro (contador, contador+1, ...) y los verifica
// - ID 0x11: responde [último contador, ~contador, 0xA5, 0x5A] para que el maestro lo verifique
// LEDs: el verde (PF10) alterna con cada trama 0x10 correcta,
//       el rojo (PF9) se prende si llega una trama mal o pasan 500 ms sin una buena
// LIN 2.x a 19200 baud, checksum mejorado. USART3: TX = PD8, RX = PD9, EN del TLIN4029 = PD10
// STM32G491VC (reloj por defecto: HSI16 = 16 MHz)
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// RCC (RM0440)
#define RCC_AHB2ENR   REG(0x4002104CUL)
#define RCC_APB1ENR1  REG(0x40021058UL)
#define RCC_GPIODEN   (1U << 3)
#define RCC_GPIOFEN   (1U << 5)
#define RCC_USART3EN  (1U << 18)

// GPIO
#define GPIOD_BASE    0x48000C00UL
#define GPIOF_BASE    0x48001400UL
#define GPIO_MODER(b)    REG((b) + 0x00)
#define GPIO_OTYPER(b)   REG((b) + 0x04)
#define GPIO_PUPDR(b)    REG((b) + 0x0C)
#define GPIO_ODR(b)      REG((b) + 0x14)
#define GPIO_BSRR(b)     REG((b) + 0x18)
#define GPIO_AFR(b, pin) REG((b) + 0x20 + ((pin) / 8) * 4)

// USART3 (reloj por defecto: PCLK1 = 16 MHz)
#define USART3_BASE   0x40004800UL
#define USART3_CR1    REG(USART3_BASE + 0x00)
#define USART3_CR2    REG(USART3_BASE + 0x04)
#define USART3_BRR    REG(USART3_BASE + 0x0C)
#define USART3_ISR    REG(USART3_BASE + 0x1C)
#define USART3_ICR    REG(USART3_BASE + 0x20)
#define USART3_RDR    REG(USART3_BASE + 0x24)
#define USART3_TDR    REG(USART3_BASE + 0x28)
#define CR1_UE        (1U << 0)
#define CR1_RE        (1U << 2)
#define CR1_TE        (1U << 3)
#define CR2_LBDL      (1U << 5)   // detección de break de 11 bits
#define CR2_LINEN     (1U << 14)
#define ISR_RXNE      (1U << 5)
#define ISR_TXE       (1U << 7)
#define ISR_LBDF      (1U << 8)
#define ICR_ERRORES   0x15FU      // limpia PE, FE, NE, ORE, IDLE, TC y LBD

// SysTick y FPU (Cortex-M4)
#define SYST_CSR      REG(0xE000E010UL)
#define SYST_RVR      REG(0xE000E014UL)
#define SYST_CVR      REG(0xE000E018UL)
#define SCB_CPACR     REG(0xE000ED88UL)

#define CPU_HZ        16000000U
#define LIN_BAUD      19200U
#define LIN_SYNC      0x55
#define ID_MAESTRO    0x10  // trama que publica el maestro
#define ID_ESCLAVO    0x11  // trama que publica el esclavo
#define LIN_LEN       4
#define BYTE_TIMEOUT_MS 5
#define RX_TIMEOUT_MS 500

#define LED_ROJO      9   // PF9
#define LED_VERDE     10  // PF10
#define LIN_TX        8   // PD8, AF7
#define LIN_RX        9   // PD9, AF7
#define LIN_EN        10  // PD10, high = TLIN4029 en modo normal

static volatile uint32_t ticks;

// Para mirar desde el debugger (panel Watch)
volatile uint32_t rx_ok;      // tramas 0x10 recibidas bien
volatile uint32_t rx_err;     // tramas que llegaron mal (sync, datos o checksum)
volatile uint32_t resp_count; // respuestas 0x11 enviadas
volatile uint32_t eco_err;    // bytes propios que no se leyeron bien en el bus

// La llama el startup antes de main
void SystemInit(void)
{
    SCB_CPACR |= (0xFU << 20);  // habilita la FPU
}

void SysTick_Handler(void) { ticks++; }

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

static void lin_init(void)
{
    gpio_af(GPIOD_BASE, LIN_TX, 7);
    gpio_af(GPIOD_BASE, LIN_RX, 7);
    GPIO_BSRR(GPIOD_BASE) = 1U << LIN_EN;  // transceiver habilitado
    gpio_output(GPIOD_BASE, LIN_EN);

    USART3_BRR = (CPU_HZ + LIN_BAUD / 2) / LIN_BAUD;
    USART3_CR2 = CR2_LINEN | CR2_LBDL;  // 8N1, modo LIN
    USART3_CR1 = CR1_TE | CR1_RE | CR1_UE;
}

// Descarta lo que haya en el receptor y limpia los errores
static void lin_flush(void)
{
    while (USART3_ISR & ISR_RXNE) (void)USART3_RDR;
    USART3_ICR = ICR_ERRORES;
}

// Devuelve el byte recibido o -1 si no llegó nada a tiempo
static int lin_getc(void)
{
    uint32_t t0 = ticks;
    while (!(USART3_ISR & ISR_RXNE)) {
        if ((ticks - t0) > BYTE_TIMEOUT_MS) return -1;
    }
    return (int)(USART3_RDR & 0xFFU);
}

// LIN es un solo cable: todo lo que se manda vuelve por RX. Devuelve 1 si el eco coincide
static int lin_putc(uint8_t b)
{
    while (!(USART3_ISR & ISR_TXE));
    USART3_TDR = b;
    if (lin_getc() != b) {
        eco_err++;
        return 0;
    }
    return 1;
}

// Identificador protegido: ID de 6 bits + 2 bits de paridad
static uint8_t lin_pid(uint8_t id)
{
    uint8_t b0 = id & 1, b1 = (id >> 1) & 1, b2 = (id >> 2) & 1;
    uint8_t b3 = (id >> 3) & 1, b4 = (id >> 4) & 1, b5 = (id >> 5) & 1;
    uint8_t p0 = b0 ^ b1 ^ b2 ^ b4;
    uint8_t p1 = !(b1 ^ b3 ^ b4 ^ b5);
    return (uint8_t)(id | (p0 << 6) | (p1 << 7));
}

// Checksum mejorado (LIN 2.x): suma con acarreo del PID y los datos, invertida
static uint8_t lin_checksum(uint8_t pid, const uint8_t *data, uint32_t len)
{
    uint32_t suma = pid;
    for (uint32_t i = 0; i < len; i++) {
        suma += data[i];
        if (suma > 0xFF) suma -= 0xFF;
    }
    return (uint8_t)~suma;
}

// Trama 0x10: lee los datos del maestro. Devuelve 1 si llegaron bien
static int recibir_contador(uint8_t *cont)
{
    uint8_t data[LIN_LEN];
    for (uint32_t i = 0; i < LIN_LEN; i++) {
        int c = lin_getc();
        if (c < 0) return 0;
        data[i] = (uint8_t)c;
    }
    int chk = lin_getc();
    if (chk != lin_checksum(lin_pid(ID_MAESTRO), data, LIN_LEN)) return 0;

    for (uint32_t i = 1; i < LIN_LEN; i++) {
        if (data[i] != (uint8_t)(data[0] + i)) return 0;
    }
    *cont = data[0];
    return 1;
}

// Trama 0x11: completa el header del maestro con la respuesta
static void responder(uint8_t cont)
{
    uint8_t data[LIN_LEN] = { cont, (uint8_t)~cont, 0xA5, 0x5A };
    for (uint32_t i = 0; i < LIN_LEN; i++) {
        if (!lin_putc(data[i])) return;
    }
    if (lin_putc(lin_checksum(lin_pid(ID_ESCLAVO), data, LIN_LEN))) resp_count++;
}

int main(void)
{
    // SysTick a 1 ms con el reloj del CPU
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x7;

    RCC_AHB2ENR |= RCC_GPIODEN | RCC_GPIOFEN;
    RCC_APB1ENR1 |= RCC_USART3EN;
    (void)RCC_APB1ENR1;  // espera a que se habilite el reloj

    gpio_output(GPIOF_BASE, LED_ROJO);
    gpio_output(GPIOF_BASE, LED_VERDE);

    lin_init();

    uint8_t ultimo = 0;
    uint32_t t_ok = ticks;
    int error = 0;

    for (;;) {
        // Cada trama arranca con un break: hasta que no llega uno, no hay nada que hacer
        if (USART3_ISR & ISR_LBDF) {
            lin_flush();  // limpia LBDF y descarta el 0x00 que deja el break

            if (lin_getc() == LIN_SYNC) {
                int pid = lin_getc();
                if (pid == lin_pid(ID_MAESTRO)) {
                    if (recibir_contador(&ultimo)) {
                        rx_ok++;
                        t_ok = ticks;
                        error = 0;
                        GPIO_ODR(GPIOF_BASE) ^= 1U << LED_VERDE;
                    } else {
                        rx_err++;
                        error = 1;
                    }
                } else if (pid == lin_pid(ID_ESCLAVO)) {
                    responder(ultimo);
                }
            } else {
                rx_err++;
                error = 1;
            }
        }

        if (error || (ticks - t_ok) > RX_TIMEOUT_MS) {
            GPIO_BSRR(GPIOF_BASE) = (1U << LED_ROJO) | (1U << (LED_VERDE + 16));  // rojo on, verde off
        } else {
            GPIO_BSRR(GPIOF_BASE) = 1U << (LED_ROJO + 16);  // rojo off
        }
    }
}
