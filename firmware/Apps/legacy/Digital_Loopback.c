// Prueba de lazo digital: unir con un cable DO2 (PC10) con DI4 (PC11)
// Cada vuelta del loop: lee PC11, prende el LED que corresponde y cambia PC10
// (LOW -> rojo PF9, HIGH -> verde PF10). Sin delays: el timing se maneja con breakpoints
// PE8 y PE10 en low habilitan los dos buffers de U5
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
#define GPIO_IDR(b)    REG((b) + 0x10)
#define GPIO_ODR(b)    REG((b) + 0x14)
#define GPIO_BSRR(b)   REG((b) + 0x18)

// FPU (Cortex-M4)
#define SCB_CPACR     REG(0xE000ED88UL)

#define OUT_PIN    10  // PC10 (DO2)
#define IN_PIN     11  // PC11 (DI4)
#define OE1        8   // PE8, enable del buffer U5 (activo en low)
#define OE2        10  // PE10, enable del buffer U5 (activo en low)
#define LED_ROJO   9   // PF9
#define LED_VERDE  10  // PF10

// La llama el startup antes de main
void SystemInit(void)
{
    SCB_CPACR |= (0xFU << 20);  // habilita la FPU
}

// Configura el pin como salida push-pull, sin pull-up/down, arrancando en low
static void gpio_output(uint32_t base, uint32_t pin)
{
    GPIO_BSRR(base) = 1U << (pin + 16);  // low antes de pasarlo a salida
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (1U << (pin * 2));
    GPIO_OTYPER(base) &= ~(1U << pin);
    GPIO_PUPDR(base) &= ~(3U << (pin * 2));
}

int main(void)
{
    RCC_AHB2ENR |= RCC_GPIOCEN | RCC_GPIOEEN | RCC_GPIOFEN;
    (void)RCC_AHB2ENR;  // espera a que se habilite el reloj

    // Buffers habilitados
    gpio_output(GPIOE_BASE, OE1);
    gpio_output(GPIOE_BASE, OE2);

    // PC11 como entrada con pull-down (si queda al aire lee low)
    GPIO_MODER(GPIOC_BASE) &= ~(3U << (IN_PIN * 2));
    GPIO_PUPDR(GPIOC_BASE) = (GPIO_PUPDR(GPIOC_BASE) & ~(3U << (IN_PIN * 2))) | (2U << (IN_PIN * 2));

    gpio_output(GPIOC_BASE, OUT_PIN);
    gpio_output(GPIOF_BASE, LED_ROJO);
    gpio_output(GPIOF_BASE, LED_VERDE);

    for (;;) {
        if (GPIO_IDR(GPIOC_BASE) & (1U << IN_PIN)) {
            GPIO_BSRR(GPIOF_BASE) = (1U << LED_VERDE) | (1U << (LED_ROJO + 16));  // verde on, rojo off
        } else {
            GPIO_BSRR(GPIOF_BASE) = (1U << LED_ROJO) | (1U << (LED_VERDE + 16));  // rojo on, verde off
        }

        GPIO_ODR(GPIOC_BASE) ^= 1U << OUT_PIN;  // cambia la salida
    }
}
