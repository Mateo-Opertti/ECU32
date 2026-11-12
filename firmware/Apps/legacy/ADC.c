// Test de entradas analógicas AN1..AN4 con el ADC1
// - Lee las 4 entradas cada 100 ms y guarda la tensión en mV en an_mv[] (verla en el Watch)
// - Los LEDs muestran la entrada elegida en CANAL_LED:
//     menos de 1.1 V  -> rojo
//     1.1 V a 2.2 V   -> rojo y verde
//     más de 2.2 V    -> verde
// Entradas: AN1 = PB11 (IN14), AN2 = PA1 (IN2), AN3 = PC1 (IN7), AN4 = PA0 (IN1)
// STM32G491VC (reloj por defecto: HSI16 = 16 MHz)
#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

// RCC (RM0440)
#define RCC_AHB2ENR   REG(0x4002104CUL)
#define RCC_GPIOAEN   (1U << 0)
#define RCC_GPIOBEN   (1U << 1)
#define RCC_GPIOCEN   (1U << 2)
#define RCC_GPIOFEN   (1U << 5)
#define RCC_ADC12EN   (1U << 13)

// GPIO
#define GPIOA_BASE    0x48000000UL
#define GPIOB_BASE    0x48000400UL
#define GPIOC_BASE    0x48000800UL
#define GPIOF_BASE    0x48001400UL
#define GPIO_MODER(b)  REG((b) + 0x00)
#define GPIO_OTYPER(b) REG((b) + 0x04)
#define GPIO_PUPDR(b)  REG((b) + 0x0C)
#define GPIO_BSRR(b)   REG((b) + 0x18)

// ADC1 y registros comunes de ADC1/ADC2
#define ADC1_BASE     0x50000000UL
#define ADC1_ISR      REG(ADC1_BASE + 0x00)
#define ADC1_CR       REG(ADC1_BASE + 0x08)
#define ADC1_SMPR1    REG(ADC1_BASE + 0x14)
#define ADC1_SMPR2    REG(ADC1_BASE + 0x18)
#define ADC1_SQR1     REG(ADC1_BASE + 0x30)
#define ADC1_DR       REG(ADC1_BASE + 0x40)
#define ADC12_CCR     REG(0x50000300UL + 0x08)
#define ISR_ADRDY     (1U << 0)
#define ISR_EOC       (1U << 2)
#define CR_ADEN       (1U << 0)
#define CR_ADSTART    (1U << 2)
#define CR_ADVREGEN   (1U << 28)
#define CR_DEEPPWD    (1U << 29)
#define CR_ADCAL      (1U << 31)

// SysTick y FPU (Cortex-M4)
#define SYST_CSR      REG(0xE000E010UL)
#define SYST_RVR      REG(0xE000E014UL)
#define SYST_CVR      REG(0xE000E018UL)
#define SCB_CPACR     REG(0xE000ED88UL)

#define CPU_HZ        16000000U
#define VDDA_MV       3300U  // referencia del ADC
#define PROMEDIO      16     // lecturas promediadas por canal
#define PERIODO_MS    100
#define CANAL_LED     0      // entrada que se ve en los LEDs: 0 = AN1 ... 3 = AN4
#define UMBRAL_BAJO   1100   // mV
#define UMBRAL_ALTO   2200   // mV

#define LED_ROJO      9   // PF9
#define LED_VERDE     10  // PF10

static const struct { uint32_t port; uint32_t pin; uint32_t canal; } entradas[4] = {
    { GPIOB_BASE, 11, 14 },  // AN1 = PB11
    { GPIOA_BASE, 1,  2  },  // AN2 = PA1
    { GPIOC_BASE, 1,  7  },  // AN3 = PC1
    { GPIOA_BASE, 0,  1  },  // AN4 = PA0
};

static volatile uint32_t ticks;

// Para mirar desde el debugger (panel Watch)
volatile uint32_t an_raw[4];  // cuentas del ADC (0..4095)
volatile uint32_t an_mv[4];   // tensión en mV: an_mv[0] = AN1 ... an_mv[3] = AN4

// La llama el startup antes de main
void SystemInit(void)
{
    SCB_CPACR |= (0xFU << 20);  // habilita la FPU
}

void SysTick_Handler(void) { ticks++; }

static void adc_init(void)
{
    // Reloj del ADC: HCLK / 1 (sincrónico), así no hace falta configurar el PLL
    ADC12_CCR = (ADC12_CCR & ~(3U << 16)) | (1U << 16);

    // Sale de deep power-down, prende el regulador y espera su arranque (20 us)
    ADC1_CR &= ~CR_DEEPPWD;
    ADC1_CR |= CR_ADVREGEN;
    for (volatile uint32_t i = 0; i < 1000; i++);

    // Calibración en modo single-ended
    ADC1_CR |= CR_ADCAL;
    while (ADC1_CR & CR_ADCAL);

    // Muestreo largo (640.5 ciclos) en todos los canales: sirve para fuentes de alta impedancia
    ADC1_SMPR1 = 0x3FFFFFFFU;
    ADC1_SMPR2 = 0x07FFFFFFU;

    ADC1_ISR = ISR_ADRDY;  // se limpia escribiendo 1
    ADC1_CR |= CR_ADEN;
    while (!(ADC1_ISR & ISR_ADRDY));
}

static uint32_t adc_read(uint32_t canal)
{
    ADC1_SQR1 = canal << 6;  // una sola conversión (L = 0) del canal pedido
    ADC1_CR |= CR_ADSTART;
    while (!(ADC1_ISR & ISR_EOC));
    return ADC1_DR;  // leer DR limpia EOC
}

int main(void)
{
    // SysTick a 1 ms con el reloj del CPU
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x7;

    RCC_AHB2ENR |= RCC_GPIOAEN | RCC_GPIOBEN | RCC_GPIOCEN | RCC_GPIOFEN | RCC_ADC12EN;
    (void)RCC_AHB2ENR;  // espera a que se habilite el reloj

    // Entradas en modo analógico, sin pull-up/down
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t b = entradas[i].port, p = entradas[i].pin;
        GPIO_MODER(b) |= 3U << (p * 2);
        GPIO_PUPDR(b) &= ~(3U << (p * 2));
    }

    // PF9 y PF10 como salida push-pull, sin pull-up/down
    GPIO_MODER(GPIOF_BASE) &= ~((3U << (LED_ROJO * 2)) | (3U << (LED_VERDE * 2)));
    GPIO_MODER(GPIOF_BASE) |=   (1U << (LED_ROJO * 2)) | (1U << (LED_VERDE * 2));
    GPIO_OTYPER(GPIOF_BASE) &= ~((1U << LED_ROJO) | (1U << LED_VERDE));
    GPIO_PUPDR(GPIOF_BASE) &= ~((3U << (LED_ROJO * 2)) | (3U << (LED_VERDE * 2)));

    adc_init();

    uint32_t t0 = ticks;

    for (;;) {
        if ((ticks - t0) < PERIODO_MS) continue;
        t0 = ticks;

        for (uint32_t i = 0; i < 4; i++) {
            uint32_t suma = 0;
            for (uint32_t n = 0; n < PROMEDIO; n++) suma += adc_read(entradas[i].canal);
            an_raw[i] = suma / PROMEDIO;
            an_mv[i] = an_raw[i] * VDDA_MV / 4095;
        }

        uint32_t mv = an_mv[CANAL_LED];
        uint32_t rojo  = mv <= UMBRAL_ALTO;
        uint32_t verde = mv >= UMBRAL_BAJO;
        GPIO_BSRR(GPIOF_BASE) = (rojo  ? (1U << LED_ROJO)  : (1U << (LED_ROJO + 16))) |
                                (verde ? (1U << LED_VERDE) : (1U << (LED_VERDE + 16)));
    }
}
