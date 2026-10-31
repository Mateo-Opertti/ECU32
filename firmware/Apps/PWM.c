// PWM por la salida digital DO2 (PC10) con el TIM8, canal 1 complementario (TIM8_CH1N, AF4)
// - 500 kHz, duty inicial 50 %
// - El duty se puede cambiar en vivo desde el debugger: modificar duty_pct en el Watch (0..100)
// PE8 y PE10 en low habilitan los dos buffers de U5
// El LED verde (PF10) se prende para indicar que el programa está corriendo
// STM32G491VC (reloj por defecto: HSI16 = 16 MHz)
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// RCC (RM0440)
#define RCC_AHB2ENR   REG(0x4002104CUL)
#define RCC_APB2ENR   REG(0x40021060UL)
#define RCC_GPIOCEN   (1U << 2)
#define RCC_GPIOEEN   (1U << 4)
#define RCC_GPIOFEN   (1U << 5)
#define RCC_TIM8EN    (1U << 13)

// GPIO
#define GPIOC_BASE    0x48000800UL
#define GPIOE_BASE    0x48001000UL
#define GPIOF_BASE    0x48001400UL
#define GPIO_MODER(b)    REG((b) + 0x00)
#define GPIO_OTYPER(b)   REG((b) + 0x04)
#define GPIO_PUPDR(b)    REG((b) + 0x0C)
#define GPIO_BSRR(b)     REG((b) + 0x18)
#define GPIO_AFR(b, pin) REG((b) + 0x20 + ((pin) / 8) * 4)

// TIM8 (reloj: PCLK2 = 16 MHz)
#define TIM8_BASE     0x40013400UL
#define TIM8_CR1      REG(TIM8_BASE + 0x00)
#define TIM8_EGR      REG(TIM8_BASE + 0x14)
#define TIM8_CCMR1    REG(TIM8_BASE + 0x18)
#define TIM8_CCER     REG(TIM8_BASE + 0x20)
#define TIM8_PSC      REG(TIM8_BASE + 0x28)
#define TIM8_ARR      REG(TIM8_BASE + 0x2C)
#define TIM8_CCR1     REG(TIM8_BASE + 0x34)
#define TIM8_BDTR     REG(TIM8_BASE + 0x44)
#define CR1_CEN       (1U << 0)
#define CR1_ARPE      (1U << 7)
#define EGR_UG        (1U << 0)
#define CCMR1_OC1PE   (1U << 3)
#define CCMR1_OC1M_PWM1 (6U << 4)
#define CCER_CC1NE    (1U << 2)
#define BDTR_MOE      (1U << 15)

// FPU (Cortex-M4)
#define SCB_CPACR     REG(0xE000ED88UL)

#define TIM_HZ        16000000U
#define PWM_HZ        500000U
#define TICK_HZ       16000000U  // el timer cuenta a 16 MHz (sin prescaler): 32 cuentas por período
#define PWM_PERIODO   (TICK_HZ / PWM_HZ)

#define OUT_PIN   10  // PC10 (DO2), AF4 = TIM8_CH1N
#define OE1       8   // PE8, enable del buffer U5 (activo en low)
#define OE2       10  // PE10, enable del buffer U5 (activo en low)
#define LED_VERDE 10  // PF10

// Para cambiar desde el debugger (panel Watch)
volatile uint32_t duty_pct = 50;

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

static void gpio_af(uint32_t base, uint32_t pin, uint32_t af)
{
    uint32_t sh = (pin % 8) * 4;
    GPIO_AFR(base, pin) = (GPIO_AFR(base, pin) & ~(0xFU << sh)) | (af << sh);
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (2U << (pin * 2));
}

static void pwm_init(void)
{
    TIM8_PSC = TIM_HZ / TICK_HZ - 1;
    TIM8_ARR = PWM_PERIODO - 1;
    TIM8_CCR1 = PWM_PERIODO * duty_pct / 100;

    TIM8_CCMR1 = CCMR1_OC1M_PWM1 | CCMR1_OC1PE;  // PWM modo 1 con precarga
    TIM8_CCER = CCER_CC1NE;  // solo la salida complementaria CH1N (no invertida)
    TIM8_BDTR = BDTR_MOE;    // los timers avanzados necesitan MOE para sacar la señal

    TIM8_CR1 = CR1_ARPE;
    TIM8_EGR = EGR_UG;  // carga PSC, ARR y CCR1
    TIM8_CR1 |= CR1_CEN;
}

int main(void)
{
    RCC_AHB2ENR |= RCC_GPIOCEN | RCC_GPIOEEN | RCC_GPIOFEN;
    RCC_APB2ENR |= RCC_TIM8EN;
    (void)RCC_APB2ENR;  // espera a que se habilite el reloj

    // Buffers habilitados
    gpio_output(GPIOE_BASE, OE1, 0);
    gpio_output(GPIOE_BASE, OE2, 0);

    pwm_init();
    gpio_af(GPIOC_BASE, OUT_PIN, 4);

    gpio_output(GPIOF_BASE, LED_VERDE, 1);

    for (;;) {
        uint32_t d = duty_pct > 100 ? 100 : duty_pct;
        TIM8_CCR1 = PWM_PERIODO * d / 100;
    }
}
