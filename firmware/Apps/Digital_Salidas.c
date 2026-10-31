// Salida digital: pone PC10 (DO2) en HIGH con los dos buffers de U5 habilitados
// PE8 y PE10 son los OE de U5, activos en low
// El LED verde (PF10) se prende para indicar que el programa está corriendo
// STM32G491VC (reloj por defecto: HSI16 = 16 MHz)
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// RCC (RM0440)
#define RCC_AHB2ENR   REG(0x4002104CUL)
#define RCC_GPIOCEN   (1U << 2)
#define RCC_GPIOEEN   (1U << 4)
#define RCC_GPIOFEN   (1U << 5)

// GPIO
#define GPIOC_BASE    0x48000800UL
#define GPIOE_BASE    0x48001000UL
#define GPIOF_BASE    0x48001400UL
#define GPIO_MODER(b)  REG((b) + 0x00)
#define GPIO_OTYPER(b) REG((b) + 0x04)
#define GPIO_PUPDR(b)  REG((b) + 0x0C)
#define GPIO_BSRR(b)   REG((b) + 0x18)

// FPU (Cortex-M4)
#define SCB_CPACR     REG(0xE000ED88UL)

#define OUT_PIN   10  // PC10 (DO2)
#define OE1       8   // PE8, enable del buffer U5 (activo en low)
#define OE2       10  // PE10, enable del buffer U5 (activo en low)
#define LED_VERDE 10  // PF10

// La llama el startup antes de main
void SystemInit(void)
{
    SCB_CPACR |= (0xFU << 20);  // habilita la FPU
}

// Configura el pin como salida push-pull, sin pull-up/down, arrancando en el nivel pedido
static void gpio_output(uint32_t base, uint32_t pin, int high)
{
    GPIO_BSRR(base) = high ? (1U << pin) : (1U << (pin + 16));  // nivel antes de pasarlo a salida
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (1U << (pin * 2));
    GPIO_OTYPER(base) &= ~(1U << pin);
    GPIO_PUPDR(base) &= ~(3U << (pin * 2));
}

int main(void)
{
    RCC_AHB2ENR |= RCC_GPIOCEN | RCC_GPIOEEN | RCC_GPIOFEN;
    (void)RCC_AHB2ENR;  // espera a que se habilite el reloj

    // Buffers habilitados
    gpio_output(GPIOE_BASE, OE1, 0);
    gpio_output(GPIOE_BASE, OE2, 0);

    gpio_output(GPIOC_BASE, OUT_PIN, 1);  // PC10 en HIGH
    gpio_output(GPIOF_BASE, LED_VERDE, 1);

    for (;;);
}
