// Pines de la placa ECU32 y helpers de GPIO
// Todos los helpers escriben con BSRR o con read-modify-write de los bits del pin:
// en GPIOB, GPIOC y GPIOE nunca se escribe un registro completo, porque ahí viven la consola y los /OE
#ifndef ECU32_PLACA_H
#define ECU32_PLACA_H

#include "ecu32_regs.h"

// LEDs
#define LED_PORT      GPIOF_BASE
#define LED_ROJO      9    // PF9 (DS1)
#define LED_VERDE     10   // PF10 (DS2)

// U5 (SN74LV244A): 1/OE habilita DO1-DO4 y 2/OE habilita DI1-DI4, activos en bajo
#define OE_PORT       GPIOE_BASE
#define OE1           8    // PE8
#define OE2           10   // PE10
#define DO1_PORT      GPIOB_BASE
#define DO1           9    // PB9
#define DO3_PORT      GPIOC_BASE
#define DO3           10   // PC10 (TIM8_CH1N con AF4)
#define DO4_PORT      GPIOC_BASE
#define DO4           12   // PC12
#define DI1_PORT      GPIOE_BASE
#define DI1           5    // PE5
#define DI3_PORT      GPIOD_BASE
#define DI3           2    // PD2
#define DI4_PORT      GPIOC_BASE
#define DI4           11   // PC11

// Consola: USART1 por J1. DO2 (PE0) = TX y DI2 (PE1) = RX, AF7. Reservados: ninguna prueba los toca
#define CONS_PORT     GPIOE_BASE
#define CONS_TX       0    // PE0 -> DO2 (J1-1)
#define CONS_RX       1    // PE1 <- DI2 (J1-6)

// CAN (TCAN1046): STB en alto = standby
#define CAN1_TX       12   // PA12, AF9
#define CAN1_RX       11   // PA11, AF9
#define CAN2_TX       6    // PB6, AF9
#define CAN2_RX       5    // PB5, AF9
#define CAN_STB_PORT  GPIOD_BASE
#define CAN1_STB      14   // PD14
#define CAN2_STB      15   // PD15

// LIN (TLIN4029): USART3
#define LIN_PORT      GPIOD_BASE
#define LIN_TX        8    // PD8, AF7
#define LIN_RX        9    // PD9, AF7
#define LIN_EN        10   // PD10, alto = modo normal

// ESP32: USART2
#define ESP_PORT      GPIOD_BASE
#define ESP_RX2       5    // PD5, AF7 (USART2_TX)

extern volatile uint32_t ticks;  // ms desde el arranque (SysTick)

// Microsegundos desde el arranque, con el contador del SysTick. Vuelve a 0 cada ~71 min:
// sirve para restar tiempos cortos (por ejemplo, el tiempo de ida y vuelta de una trama)
static inline uint32_t micros(void)
{
    uint32_t t, c;
    do {
        t = ticks;
        c = SYST_CVR;
    } while (t != ticks);  // si justo pasó un ms, se vuelve a leer
    return t * 1000U + (SYST_RVR - c) / (CPU_HZ / 1000000U);
}

static inline void gpio_poner(uint32_t base, uint32_t pin, int alto)
{
    GPIO_BSRR(base) = alto ? (1U << pin) : (1U << (pin + 16));
}

static inline int gpio_leer(uint32_t base, uint32_t pin)
{
    return (GPIO_IDR(base) >> pin) & 1U;
}

// Salida push-pull, sin pull-up/down, arrancando en el nivel pedido
static inline void gpio_salida(uint32_t base, uint32_t pin, int alto)
{
    gpio_poner(base, pin, alto);  // nivel antes de pasarlo a salida
    GPIO_OTYPER(base) &= ~(1U << pin);
    GPIO_PUPDR(base) &= ~(3U << (pin * 2));
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (1U << (pin * 2));
}

// pupd: 0 = nada, 1 = pull-up, 2 = pull-down
static inline void gpio_entrada(uint32_t base, uint32_t pin, uint32_t pupd)
{
    GPIO_PUPDR(base) = (GPIO_PUPDR(base) & ~(3U << (pin * 2))) | (pupd << (pin * 2));
    GPIO_MODER(base) &= ~(3U << (pin * 2));
}

// Función alternativa, push-pull y velocidad alta
static inline void gpio_af(uint32_t base, uint32_t pin, uint32_t af)
{
    uint32_t sh = (pin % 8) * 4;
    GPIO_AFR(base, pin) = (GPIO_AFR(base, pin) & ~(0xFU << sh)) | (af << sh);
    GPIO_OTYPER(base) &= ~(1U << pin);
    GPIO_OSPEEDR(base) = (GPIO_OSPEEDR(base) & ~(3U << (pin * 2))) | (2U << (pin * 2));
    GPIO_MODER(base) = (GPIO_MODER(base) & ~(3U << (pin * 2))) | (2U << (pin * 2));
}

// Modo analógico: el estado de reset, sin consumo ni pull
static inline void gpio_analogico(uint32_t base, uint32_t pin)
{
    GPIO_PUPDR(base) &= ~(3U << (pin * 2));
    GPIO_MODER(base) |= 3U << (pin * 2);
}

static inline void leds(int rojo, int verde)
{
    GPIO_BSRR(LED_PORT) = (1U << (rojo ? LED_ROJO : LED_ROJO + 16)) |
                          (1U << (verde ? LED_VERDE : LED_VERDE + 16));
}

static inline void led_alternar(uint32_t pin)
{
    gpio_poner(LED_PORT, pin, !((GPIO_ODR(LED_PORT) >> pin) & 1U));
}

#endif
