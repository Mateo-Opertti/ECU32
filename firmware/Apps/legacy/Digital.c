// Entrada digital PC11 -> LEDs: high prende verde (PF10), low prende rojo (PF9)
// STM32G491VC (reloj por defecto: HSI16 = 16 MHz)
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// RCC (RM0440)
#define RCC_AHB2ENR   REG(0x4002104CUL)
#define RCC_GPIOCEN   (1U << 2)
#define RCC_GPIOFEN   (1U << 5)
#define RCC_GPIOEEN   (1U << 4)

// GPIOE
#define GPIOE_BASE    0x48001000UL
#define GPIOE_MODER   REG(GPIOE_BASE + 0x00)
#define GPIOE_OTYPER  REG(GPIOE_BASE + 0x04)
#define GPIOE_PUPDR   REG(GPIOE_BASE + 0x0C)
#define GPIOE_BSRR    REG(GPIOE_BASE + 0x18)

// GPIOC
#define GPIOC_BASE    0x48000800UL
#define GPIOC_MODER   REG(GPIOC_BASE + 0x00)
#define GPIOC_PUPDR   REG(GPIOC_BASE + 0x0C)
#define GPIOC_IDR     REG(GPIOC_BASE + 0x10)

// GPIOF
#define GPIOF_BASE    0x48001400UL
#define GPIOF_MODER   REG(GPIOF_BASE + 0x00)
#define GPIOF_OTYPER  REG(GPIOF_BASE + 0x04)
#define GPIOF_PUPDR   REG(GPIOF_BASE + 0x0C)
#define GPIOF_ODR     REG(GPIOF_BASE + 0x14)
#define GPIOF_BSRR    REG(GPIOF_BASE + 0x18)

// SysTick y FPU (Cortex-M4)
#define SYST_CSR      REG(0xE000E010UL)
#define SYST_RVR      REG(0xE000E014UL)
#define SYST_CVR      REG(0xE000E018UL)
#define SCB_CPACR     REG(0xE000ED88UL)

#define CPU_HZ 16000000U
#define IN_PIN    11  // PC11
#define LED_ROJO  9   // PF9
#define LED_VERDE 10  // PF10
#define IN_EN     10  // PE10, enable del buffer de inputs (activo en low)

static volatile uint32_t ticks;

// La llama el startup antes de main
void SystemInit(void)
{
    SCB_CPACR |= (0xFU << 20);  // habilita la FPU
}

void SysTick_Handler(void) { ticks++; }

int main(void)
{
    // SysTick a 1 ms con el reloj del CPU
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x7;

    RCC_AHB2ENR |= RCC_GPIOCEN | RCC_GPIOFEN;
    (void)RCC_AHB2ENR;  // espera a que se habilite el reloj

    // PE10 como salida push-pull en low para habilitar el buffer de inputs
    RCC_AHB2ENR |= RCC_GPIOEEN;
    (void)RCC_AHB2ENR;
    GPIOE_BSRR = 1U << (IN_EN + 16);  // low antes de pasarlo a salida
    GPIOE_MODER &= ~(3U << (IN_EN * 2));
    GPIOE_MODER |=  (1U << (IN_EN * 2));
    GPIOE_OTYPER &= ~(1U << IN_EN);
    GPIOE_PUPDR &= ~(3U << (IN_EN * 2));

    // PC11 como entrada con pull-down (si queda al aire lee low)
    GPIOC_MODER &= ~(3U << (IN_PIN * 2));
    GPIOC_PUPDR &= ~(3U << (IN_PIN * 2));
    GPIOC_PUPDR |=  (2U << (IN_PIN * 2));

    // PF9 y PF10 como salida push-pull, sin pull-up/down
    GPIOF_MODER &= ~((3U << (LED_ROJO * 2)) | (3U << (LED_VERDE * 2)));
    GPIOF_MODER |=   (1U << (LED_ROJO * 2)) | (1U << (LED_VERDE * 2));
    GPIOF_OTYPER &= ~((1U << LED_ROJO) | (1U << LED_VERDE));
    GPIOF_PUPDR &= ~((3U << (LED_ROJO * 2)) | (3U << (LED_VERDE * 2)));

    for (;;) {
        if (GPIOC_IDR & (1U << IN_PIN)) {
            GPIOF_BSRR = (1U << LED_VERDE) | (1U << (LED_ROJO + 16));  // verde on, rojo off
        } else {
            GPIOF_BSRR = (1U << LED_ROJO) | (1U << (LED_VERDE + 16));  // rojo on, verde off
        }
    }
}
