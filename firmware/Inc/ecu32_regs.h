// Registros del STM32G491VC usados por el firmware (RM0440), sin HAL
// Reloj por defecto: HSI16 = 16 MHz para el CPU, PCLK1 y PCLK2
#ifndef ECU32_REGS_H
#define ECU32_REGS_H

#include <stdint.h>

#define REG(addr) (*(volatile uint32_t *)(addr))

#define CPU_HZ        16000000U
#define PCLK1_HZ      16000000U   // USART2, USART3
#define PCLK2_HZ      16000000U   // USART1, TIM8

// RCC
#define RCC_BASE      0x40021000UL
#define RCC_CR        REG(RCC_BASE + 0x00)
#define RCC_AHB2ENR   REG(RCC_BASE + 0x4C)
#define RCC_APB1ENR1  REG(RCC_BASE + 0x58)
#define RCC_APB2ENR   REG(RCC_BASE + 0x60)
#define RCC_CCIPR     REG(RCC_BASE + 0x88)
#define RCC_CSR       REG(RCC_BASE + 0x94)
#define RCC_HSEON     (1U << 16)
#define RCC_HSERDY    (1U << 17)
// AHB2ENR
#define RCC_GPIOAEN   (1U << 0)
#define RCC_GPIOBEN   (1U << 1)
#define RCC_GPIOCEN   (1U << 2)
#define RCC_GPIODEN   (1U << 3)
#define RCC_GPIOEEN   (1U << 4)
#define RCC_GPIOFEN   (1U << 5)
#define RCC_ADC12EN   (1U << 13)
// APB1ENR1
#define RCC_RTCAPBEN  (1U << 10)  // reloj de los registros de RTC y TAMP
#define RCC_USART2EN  (1U << 17)
#define RCC_USART3EN  (1U << 18)
#define RCC_FDCANEN   (1U << 25)  // uno solo para FDCAN1 y FDCAN2
#define RCC_PWREN     (1U << 28)
// APB2ENR
#define RCC_TIM8EN    (1U << 13)
#define RCC_USART1EN  (1U << 14)
// CSR: causa del reset (se borran con RMVF)
#define CSR_RMVF      (1U << 23)
#define CSR_OBLRSTF   (1U << 25)
#define CSR_PINRSTF   (1U << 26)
#define CSR_BORRSTF   (1U << 27)
#define CSR_SFTRSTF   (1U << 28)
#define CSR_IWDGRSTF  (1U << 29)
#define CSR_WWDGRSTF  (1U << 30)
#define CSR_LPWRRSTF  (1U << 31)

// GPIO
#define GPIOA_BASE    0x48000000UL
#define GPIOB_BASE    0x48000400UL
#define GPIOC_BASE    0x48000800UL
#define GPIOD_BASE    0x48000C00UL
#define GPIOE_BASE    0x48001000UL
#define GPIOF_BASE    0x48001400UL
#define GPIO_MODER(b)    REG((b) + 0x00)
#define GPIO_OTYPER(b)   REG((b) + 0x04)
#define GPIO_OSPEEDR(b)  REG((b) + 0x08)
#define GPIO_PUPDR(b)    REG((b) + 0x0C)
#define GPIO_IDR(b)      REG((b) + 0x10)
#define GPIO_ODR(b)      REG((b) + 0x14)
#define GPIO_BSRR(b)     REG((b) + 0x18)
#define GPIO_AFR(b, pin) REG((b) + 0x20 + ((pin) / 8) * 4)

// USART (mismos offsets en USART1, USART2 y USART3)
#define USART1_BASE   0x40013800UL
#define USART2_BASE   0x40004400UL
#define USART3_BASE   0x40004800UL
#define USART_CR1(b)  REG((b) + 0x00)
#define USART_CR2(b)  REG((b) + 0x04)
#define USART_CR3(b)  REG((b) + 0x08)
#define USART_BRR(b)  REG((b) + 0x0C)
#define USART_RQR(b)  REG((b) + 0x18)
#define USART_ISR(b)  REG((b) + 0x1C)
#define USART_ICR(b)  REG((b) + 0x20)
#define USART_RDR(b)  REG((b) + 0x24)
#define USART_TDR(b)  REG((b) + 0x28)
#define CR1_UE        (1U << 0)
#define CR1_RE        (1U << 2)
#define CR1_TE        (1U << 3)
#define CR1_RXNEIE    (1U << 5)
#define CR1_TXEIE     (1U << 7)
#define CR2_LBDL      (1U << 5)   // detección de break de 11 bits
#define CR2_LINEN     (1U << 14)
#define CR3_EIE       (1U << 0)
#define RQR_SBKRQ     (1U << 1)
#define ISR_PE        (1U << 0)
#define ISR_FE        (1U << 1)
#define ISR_NE        (1U << 2)
#define ISR_ORE       (1U << 3)
#define ISR_RXNE      (1U << 5)
#define ISR_TC        (1U << 6)
#define ISR_TXE       (1U << 7)
#define ISR_LBDF      (1U << 8)
#define ISR_SBKF      (1U << 18)
#define ICR_ERRORES   0x15FU      // limpia PE, FE, NE, ORE, IDLE, TC y LBD

// FDCAN (FDCAN1 y FDCAN2 comparten reloj de kernel, enable en RCC y message RAM)
#define FDCAN1_BASE   0x40006400UL
#define FDCAN2_BASE   0x40006800UL
#define FDCAN_DBTP(b)   REG((b) + 0x00C)
#define FDCAN_CCCR(b)   REG((b) + 0x018)
#define FDCAN_NBTP(b)   REG((b) + 0x01C)
#define FDCAN_ECR(b)    REG((b) + 0x040)  // contadores de error: TEC [7:0], REC [14:8]
#define FDCAN_PSR(b)    REG((b) + 0x044)
#define FDCAN_TDCR(b)   REG((b) + 0x048)
#define FDCAN_RXGFC(b)  REG((b) + 0x080)
#define FDCAN_RXF0S(b)  REG((b) + 0x090)
#define FDCAN_RXF0A(b)  REG((b) + 0x094)
#define FDCAN_TXFQS(b)  REG((b) + 0x0C4)
#define FDCAN_TXBRP(b)  REG((b) + 0x0C8)
#define FDCAN_TXBAR(b)  REG((b) + 0x0CC)
#define FDCAN_TXBCR(b)  REG((b) + 0x0D0)
#define CCCR_INIT     (1U << 0)
#define CCCR_CCE      (1U << 1)
#define CCCR_DAR      (1U << 6)   // sin retransmisión automática
#define CCCR_FDOE     (1U << 8)
#define CCCR_BRSE     (1U << 9)
#define TXFQS_TFQF    (1U << 21)
#define PSR_EP        (1U << 5)   // error pasivo
#define PSR_BO        (1U << 7)   // bus off

// Message RAM (SRAMCAN): 0x350 bytes por instancia, layout fijo en el G4
#define SRAMCAN_BASE  0x4000A400UL
#define SRAMCAN_SIZE  0x350UL
#define MRAM(b)       (SRAMCAN_BASE + ((b) == FDCAN1_BASE ? 0 : SRAMCAN_SIZE))
#define MRAM_FLSSA    0x000UL     // filtros de ID estándar
#define MRAM_RXF0     0x0B0UL
#define MRAM_TXBUF    0x278UL
#define MRAM_ELEM     72UL        // 2 words de header + 64 bytes de datos

// Header de las tramas en la message RAM
#define ELEM_XTD      (1U << 30)
#define ELEM_FDF      (1U << 21)
#define ELEM_BRS      (1U << 20)

// PWR
#define PWR_BASE      0x40007000UL
#define PWR_CR1       REG(PWR_BASE + 0x00)
#define PWR_CR3       REG(PWR_BASE + 0x08)
#define PWR_CR4       REG(PWR_BASE + 0x0C)
#define PWR_SR1       REG(PWR_BASE + 0x10)
#define PWR_SCR       REG(PWR_BASE + 0x18)
#define CR1_LPMS_MASK 7U
#define CR1_LPMS_STANDBY 3U
#define CR1_DBP       (1U << 8)   // habilita la escritura del dominio de backup (TAMP)
#define WKUP2         (1U << 1)   // mismo bit en CR3 (EWUP), CR4 (WP), SR1 (WUF) y SCR (CWUF)
#define WKUP3         (1U << 2)
#define SR1_SBF       (1U << 8)   // la placa estuvo en Standby
#define SCR_TODO      0x11FU      // limpia WUF1 a WUF5 y SBF

// TAMP: registros de backup, sobreviven al reset y al Standby
#define TAMP_BASE     0x40002400UL
#define TAMP_BKP0R    REG(TAMP_BASE + 0x100)

// FLASH (categoría 4: un solo banco, páginas de 2 KB)
#define FLASH_BASE_R  0x40022000UL
#define FLASH_ACR     REG(FLASH_BASE_R + 0x00)
#define FLASH_KEYR    REG(FLASH_BASE_R + 0x08)
#define FLASH_SR      REG(FLASH_BASE_R + 0x10)
#define FLASH_CR      REG(FLASH_BASE_R + 0x14)
#define FLASH_ECCR    REG(FLASH_BASE_R + 0x18)
#define FLASH_KEY1    0x45670123U
#define FLASH_KEY2    0xCDEF89ABU
#define ACR_DCEN      (1U << 10)
#define ACR_DCRST     (1U << 12)
#define SR_BSY        (1U << 16)
#define SR_ERRORES    0xC3FAU     // OPERR, PROGERR, WRPERR, PGAERR, SIZERR, PGSERR, MISERR, FASTERR, RDERR, OPTVERR
#define FCR_PG        (1U << 0)
#define FCR_PER       (1U << 1)
#define FCR_PNB_SH    3
#define FCR_STRT      (1U << 16)
#define FCR_LOCK      (1U << 31)
#define ECCR_ECCD     (1U << 31)  // error de ECC doble (dispara NMI)

// Timers con los mismos offsets: TIM4 (PCLK1), TIM8 y TIM16 (PCLK2)
#define TIM4_BASE     0x40000800UL
#define TIM16_BASE    0x40014400UL
#define TIM_CR1(b)    REG((b) + 0x00)
#define TIM_EGR(b)    REG((b) + 0x14)
#define TIM_CCMR1(b)  REG((b) + 0x18)
#define TIM_CCMR2(b)  REG((b) + 0x1C)
#define TIM_CCER(b)   REG((b) + 0x20)
#define TIM_PSC(b)    REG((b) + 0x28)
#define TIM_ARR(b)    REG((b) + 0x2C)
#define TIM_CCR1(b)   REG((b) + 0x34)
#define TIM_CCR3(b)   REG((b) + 0x3C)
#define TIM_CCR4(b)   REG((b) + 0x40)
#define TIM_BDTR(b)   REG((b) + 0x44)
#define RCC_TIM4EN    (1U << 2)    // APB1ENR1
#define RCC_TIM16EN   (1U << 17)   // APB2ENR
#define RCC_SYSCFGEN  (1U << 0)    // APB2ENR

// EXTI y SYSCFG (líneas de interrupción de los pines)
#define EXTI_BASE     0x40010400UL
#define EXTI_IMR1     REG(EXTI_BASE + 0x00)
#define EXTI_RTSR1    REG(EXTI_BASE + 0x08)
#define EXTI_FTSR1    REG(EXTI_BASE + 0x0C)
#define EXTI_PR1      REG(EXTI_BASE + 0x14)
#define SYSCFG_EXTICR(n) REG(0x40010000UL + 0x08 + 4 * (n))  // n = 0..3: líneas 4n..4n+3
#define EXTI1_IRQn      7
#define EXTI2_IRQn      8
#define EXTI9_5_IRQn    23
#define EXTI15_10_IRQn  40
#define USART2_IRQn     38

// CRC (CRC-32 con polinomio 0x04C11DB7, valor inicial 0xFFFFFFFF, sin reflejar)
#define RCC_AHB1ENR   REG(RCC_BASE + 0x48)
#define RCC_CRCEN     (1U << 12)
#define CRC_DR        REG(0x40023000UL)
#define CRC_DR8       (*(volatile uint8_t *)0x40023000UL)
#define CRC_CR        REG(0x40023008UL)

// VREFINT: canal 18 del ADC1, calibrado de fábrica con VDDA = 3,0 V
#define ADC_CCR_VREFEN  (1U << 22)
#define ADC_CANAL_VREFINT 18U
#define VREFINT_CAL   (*(volatile uint16_t *)0x1FFF75AAUL)

// TIM8 (reloj: PCLK2)
#define TIM8_BASE     0x40013400UL
#define TIM8_CR1      REG(TIM8_BASE + 0x00)
#define TIM8_EGR      REG(TIM8_BASE + 0x14)
#define TIM8_CCMR1    REG(TIM8_BASE + 0x18)
#define TIM8_CCER     REG(TIM8_BASE + 0x20)
#define TIM8_PSC      REG(TIM8_BASE + 0x28)
#define TIM8_ARR      REG(TIM8_BASE + 0x2C)
#define TIM8_CCR1     REG(TIM8_BASE + 0x34)
#define TIM8_BDTR     REG(TIM8_BASE + 0x44)

// ADC1 y registros comunes de ADC1/ADC2
#define ADC1_BASE     0x50000000UL
#define ADC1_ISR      REG(ADC1_BASE + 0x00)
#define ADC1_CR       REG(ADC1_BASE + 0x08)
#define ADC1_SMPR1    REG(ADC1_BASE + 0x14)
#define ADC1_SMPR2    REG(ADC1_BASE + 0x18)
#define ADC1_SQR1     REG(ADC1_BASE + 0x30)
#define ADC1_DR       REG(ADC1_BASE + 0x40)
#define ADC12_CCR     REG(0x50000300UL + 0x08)
// ADC1 y ADC2 con los mismos offsets (AN1..AN4 son ADC12_INx: los dos los pueden medir)
#define ADC2_BASE     0x50000100UL
#define ADC_ISR(b)    REG((b) + 0x00)
#define ADC_CR(b)     REG((b) + 0x08)
#define ADC_SMPR1(b)  REG((b) + 0x14)
#define ADC_SMPR2(b)  REG((b) + 0x18)
#define ADC_SQR1(b)   REG((b) + 0x30)
#define ADC_DR(b)     REG((b) + 0x40)
#define ADC_ISR_ADRDY (1U << 0)
#define ADC_ISR_EOC   (1U << 2)
#define ADC_CR_ADEN   (1U << 0)
#define ADC_CR_ADDIS  (1U << 1)
#define ADC_CR_ADSTART (1U << 2)
#define ADC_CR_ADSTP  (1U << 4)
#define ADC_CR_ADVREGEN (1U << 28)
#define ADC_CR_DEEPPWD (1U << 29)
#define ADC_CR_ADCAL  (1U << 31)

// Cortex-M4: SysTick, SCB y NVIC
#define SYST_CSR      REG(0xE000E010UL)
#define SYST_RVR      REG(0xE000E014UL)
#define SYST_CVR      REG(0xE000E018UL)
#define SCB_SCR       REG(0xE000ED10UL)
#define SCB_CPACR     REG(0xE000ED88UL)
#define SCR_SLEEPDEEP (1U << 2)
#define NVIC_ISER(n)  REG(0xE000E100UL + 4 * (n))
#define NVIC_ICER(n)  REG(0xE000E180UL + 4 * (n))
#define NVIC_ICPR(n)  REG(0xE000E280UL + 4 * (n))
#define USART1_IRQn   37

static inline void nvic_habilitar(uint32_t irq)    { NVIC_ISER(irq / 32) = 1U << (irq % 32); }
static inline void nvic_deshabilitar(uint32_t irq) { NVIC_ICER(irq / 32) = 1U << (irq % 32); }

#endif
