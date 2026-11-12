// PWM: 500 kHz por DO3 (PC10) con el TIM8, canal 1 complementario (TIM8_CH1N, AF4)
// El duty se cambia con "duty <0-100>" (arranca en 50 %). El LED verde queda prendido
// U5 ya está habilitado desde el arranque

#define PWM_HZ        500000U
#define PWM_PERIODO   (PCLK2_HZ / PWM_HZ)  // el timer cuenta a 16 MHz (sin prescaler): 32 cuentas

static void pwm_init(void)
{
    RCC_APB2ENR |= RCC_TIM8EN;
    (void)RCC_APB2ENR;

    uint32_t d = pwm_duty > 100 ? 100 : pwm_duty;
    TIM8_PSC = 0;
    TIM8_ARR = PWM_PERIODO - 1;
    TIM8_CCR1 = PWM_PERIODO * d / 100;

    TIM8_CCMR1 = (6U << 4) | (1U << 3);  // PWM modo 1 (OC1M) con precarga (OC1PE)
    TIM8_CCER = 1U << 2;                 // solo la salida complementaria CH1N (CC1NE), no invertida
    TIM8_BDTR = 1U << 15;                // MOE: los timers avanzados lo necesitan para sacar la señal

    TIM8_CR1 = 1U << 7;   // ARPE
    TIM8_EGR = 1U;        // UG: carga PSC, ARR y CCR1
    TIM8_CR1 |= 1U;       // CEN

    gpio_af(DO3_PORT, DO3, 4);
    leds(0, 1);
}

static void pwm_step(void)
{
    uint32_t d = pwm_duty > 100 ? 100 : pwm_duty;
    TIM8_CCR1 = PWM_PERIODO * d / 100;
}

static void pwm_deinit(void)
{
    gpio_salida(DO3_PORT, DO3, 0);  // primero el pin: DO3 pasa directo a bajo, sin quedar al aire
    TIM8_CR1 = 0;
    TIM8_BDTR = 0;
    TIM8_CCER = 0;
    RCC_APB2ENR &= ~RCC_TIM8EN;
}

static void pwm_estado(void)
{
    cprintf("  500 kHz, duty %u %%\n", pwm_duty > 100 ? 100 : pwm_duty);
}

static const prueba_t p_pwm = {
    .nombre = "pwm", .archivo = "PWM",
    .descripcion = "PWM de 500 kHz por DO3 (PC10, TIM8_CH1N); duty con \"duty\"",
    .init = pwm_init, .step = pwm_step, .deinit = pwm_deinit, .estado = pwm_estado,
};
