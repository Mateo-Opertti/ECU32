// Prende y apaga los dos LEDs juntos cada 500 ms: PF9 (rojo) y PF10 (verde)
// STM32G491VC (reloj por defecto: HSI16 = 16 MHz)
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// RCC (RM0440)
#define RCC_AHB2ENR   REG(0x4002104CUL)
#define RCC_GPIOFEN   (1U << 5)

// GPIOF
#define GPIOF_BASE    0x48001400UL
#define GPIOF_MODER   REG(GPIOF_BASE + 0x00)
#define GPIOF_OTYPER  REG(GPIOF_BASE + 0x04)
#define GPIOF_PUPDR   REG(GPIOF_BASE + 0x0C)
#define GPIOF_ODR     REG(GPIOF_BASE + 0x14)

// SysTick y FPU (Cortex-M4)
#define SYST_CSR      REG(0xE000E010UL)
#define SYST_RVR      REG(0xE000E014UL)
#define SYST_CVR      REG(0xE000E018UL)
#define SCB_CPACR     REG(0xE000ED88UL)

#define CPU_HZ    16000000U
#define BLINK_MS  500
#define LED_ROJO  9   // PF9
#define LED_VERDE 10  // PF10

static volatile uint32_t ticks;

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

int main(void)
{
    // SysTick a 1 ms con el reloj del CPU
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x7;

    RCC_AHB2ENR |= RCC_GPIOFEN;
    (void)RCC_AHB2ENR;  // espera a que se habilite el reloj

    // PF9 y PF10 como salida push-pull, sin pull-up/down
    GPIOF_MODER &= ~((3U << (LED_ROJO * 2)) | (3U << (LED_VERDE * 2)));
    GPIOF_MODER |=   (1U << (LED_ROJO * 2)) | (1U << (LED_VERDE * 2));
    GPIOF_OTYPER &= ~((1U << LED_ROJO) | (1U << LED_VERDE));
    GPIOF_PUPDR &= ~((3U << (LED_ROJO * 2)) | (3U << (LED_VERDE * 2)));

    for (;;) {
        GPIOF_ODR ^= (1U << LED_ROJO) | (1U << LED_VERDE);  // prende o apaga los dos
        delay_ms(BLINK_MS);
    }
}
