// Test de wake-up por CAN1: la placa se duerme y la despierta la actividad en el bus
// Al arrancar: LED rojo (PF9) prendido 3 s, se apaga y el MCU entra en Standby
//   (esos 3 s son para ver que arrancó y para poder reprogramarla: dormida no responde al ST-LINK)
// Cuando llega algo por CAN1: el TCAN1046 detecta el patrón de wake-up (WUP) y baja RXD1,
//   que entra por R61 a PE6 (WKUP3) y despierta al MCU. Desde ahí el verde (PF10) alterna cada 250 ms
// Para repetir la prueba: reset con el bus en silencio
// No usa el FDCAN: no recibe ni confirma tramas, solo prueba el camino bus -> transceiver -> WKUP3
// Para despertarla sirve otra placa con CAN_Maestro.c, unidas por CANH1, CANL1 y GND
// STM32G491VC (reloj por defecto: HSI16 = 16 MHz)
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// RCC (RM0440)
#define RCC_AHB2ENR   REG(0x4002104CUL)
#define RCC_APB1ENR1  REG(0x40021058UL)
#define RCC_GPIODEN   (1U << 3)
#define RCC_GPIOFEN   (1U << 5)
#define RCC_PWREN     (1U << 28)

// GPIO
#define GPIOD_BASE    0x48000C00UL
#define GPIOF_BASE    0x48001400UL
#define GPIO_MODER(b)    REG((b) + 0x00)
#define GPIO_OTYPER(b)   REG((b) + 0x04)
#define GPIO_PUPDR(b)    REG((b) + 0x0C)
#define GPIO_ODR(b)      REG((b) + 0x14)
#define GPIO_BSRR(b)     REG((b) + 0x18)

// PWR
#define PWR_BASE      0x40007000UL
#define PWR_CR1       REG(PWR_BASE + 0x00)
#define PWR_CR3       REG(PWR_BASE + 0x08)
#define PWR_CR4       REG(PWR_BASE + 0x0C)
#define PWR_SR1       REG(PWR_BASE + 0x10)
#define PWR_SCR       REG(PWR_BASE + 0x18)
#define LPMS_MASCARA  7U
#define LPMS_STANDBY  3U
#define WKUP3         (1U << 2)   // mismo bit en CR3 (EWUP3), CR4 (WP3), SR1 (WUF3) y SCR (CWUF3)
#define SBF           (1U << 8)   // SR1: la placa estuvo en Standby
#define SCR_TODO      0x11FU      // limpia WUF1 a WUF5 y SBF

// SysTick, FPU y modo deep sleep (Cortex-M4)
#define SYST_CSR      REG(0xE000E010UL)
#define SYST_RVR      REG(0xE000E014UL)
#define SYST_CVR      REG(0xE000E018UL)
#define SCB_SCR       REG(0xE000ED10UL)
#define SCB_CPACR     REG(0xE000ED88UL)
#define SCR_SLEEPDEEP (1U << 2)

#define CPU_HZ        16000000U
#define AVISO_MS      3000
#define PARPADEO_MS   250

#define LED_ROJO      9   // PF9
#define LED_VERDE     10  // PF10
#define CAN1_STB      14  // PD14, high = transceiver en standby

static volatile uint32_t ticks;

// Para mirar desde el debugger (panel Watch)
volatile uint32_t sr1_inicial;  // PWR_SR1 al arrancar: bit 8 = venía de Standby, bit 2 = la despertó CAN1

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
    GPIO_BSRR(base) = 1U << (pin + 16);  // low antes de pasarlo a salida
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (1U << (pin * 2));
    GPIO_OTYPER(base) &= ~(1U << pin);
    GPIO_PUPDR(base) &= ~(3U << (pin * 2));
}

static void systick_init(void)
{
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x7;
}

// Si el Standby funciona no vuelve: al despertar el MCU arranca de nuevo desde el reset
static void dormir(void)
{
    // Pasa por modo normal y vuelve a standby: RXD queda en alto hasta un WUP nuevo
    gpio_output(GPIOD_BASE, CAN1_STB);
    delay_ms(1);
    GPIO_BSRR(GPIOD_BASE) = 1U << CAN1_STB;
    delay_ms(10);

    PWR_CR4 |= WKUP3;  // flanco descendente: el WUP baja RXD1
    PWR_CR3 |= WKUP3;  // habilita WKUP3 (PE6)
    PWR_CR1 = (PWR_CR1 & ~LPMS_MASCARA) | LPMS_STANDBY;
    SCB_SCR |= SCR_SLEEPDEEP;

    SYST_CSR = 0;       // sin esto una interrupción del SysTick cancelaría el wfi
    PWR_SCR = SCR_TODO; // las banderas se limpian al final, con WKUP3 ya configurado

    __asm volatile ("dsb");
    __asm volatile ("wfi");

    // Llegar acá significa que el bus se movió justo antes de dormir: cuenta como wake-up
    SCB_SCR &= ~SCR_SLEEPDEEP;
    systick_init();
}

int main(void)
{
    systick_init();

    RCC_AHB2ENR |= RCC_GPIODEN | RCC_GPIOFEN;
    RCC_APB1ENR1 |= RCC_PWREN;
    (void)RCC_APB1ENR1;  // espera a que se habilite el reloj

    gpio_output(GPIOF_BASE, LED_ROJO);
    gpio_output(GPIOF_BASE, LED_VERDE);

    // Las banderas sobreviven al reset. Con SBF y sin WUF3 fue un reset normal estando dormida
    sr1_inicial = PWR_SR1;
    uint32_t desperto = (sr1_inicial & SBF) && (sr1_inicial & WKUP3);
    PWR_SCR = SCR_TODO;

    if (!desperto) {
        GPIO_BSRR(GPIOF_BASE) = 1U << LED_ROJO;
        delay_ms(AVISO_MS);
        GPIO_BSRR(GPIOF_BASE) = 1U << (LED_ROJO + 16);
        dormir();
    }

    for (;;) {
        GPIO_ODR(GPIOF_BASE) ^= 1U << LED_VERDE;
        delay_ms(PARPADEO_MS);
    }
}
