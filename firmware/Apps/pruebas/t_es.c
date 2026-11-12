// Prueba "es": entradas y salidas digitales a mano (plan DIG-01, DIG-02 y DIG-03)
// Salidas (por U5, 4,7 kOhm en serie):
//   DO1 = PB9  nivel o PWM con TIM4_CH4 (AF2)
//   DO2 = PE0  es el TX de la consola: en reposo está en alto (sirve para medir VOH). Para bajarla
//              o sacar PWM (TIM16_CH1, AF4) se "presta" por unos segundos y la consola no imprime
//   DO3 = PC10 nivel o PWM con TIM8_CH1N (AF4)
//   DO4 = PC12 nivel o PWM con TIM8_CH3N (AF4). DO3 y DO4 comparten el TIM8: misma frecuencia
// Entradas: DI1 = PE5, DI2 = PE1 (RX de la consola), DI3 = PD2, DI4 = PC11
//   Se informa cada cambio de nivel (sirve para registrar el umbral con una rampa lenta)
//   "frec on" mide la frecuencia de cada entrada contando flancos de subida con EXTI en ventanas
//   de 1 s. A 100 kHz cada flanco es una interrupción: medir de a una entrada por vez

#define ES_DO2_SEG_DEF  10U     // segundos que se presta DO2 si no se indica
#define ES_DO2_SEG_MAX  3600U
#define ES_DI_MIN_MS    50U     // como mucho un aviso de cambio de entrada cada 50 ms

typedef struct { uint32_t port, pin; } es_pin_t;
static const es_pin_t es_do_pin[5] = {
    { 0, 0 }, { DO1_PORT, DO1 }, { CONS_PORT, CONS_TX }, { DO3_PORT, DO3 }, { DO4_PORT, DO4 }
};
static const es_pin_t es_di_pin[5] = {
    { 0, 0 }, { DI1_PORT, DI1 }, { CONS_PORT, CONS_RX }, { DI3_PORT, DI3 }, { DI4_PORT, DI4 }
};

static uint32_t es_pwm[5], es_nivel[5], es_hz[5], es_duty[5];  // es_pwm: 1 = PWM, 0 = nivel fijo
static uint32_t es_tim8_hz;

// DO2 prestada a la prueba
static int es_do2_pedida, es_do2_tomada;
static uint32_t es_do2_seg, es_do2_desde;

// Entradas
static volatile uint32_t es_cuenta[5];  // flancos contados por EXTI
static uint32_t es_cuenta_ant[5], es_frec[5];
static int es_frec_on;
static uint32_t es_t_frec, es_t_di, es_t_res, es_di_ult;
static volatile uint32_t es_cambios;

// ---------------------------------------------------------------------------------------------
// Timers

// PSC y ARR para "hz" con un reloj de "clk". Devuelve las cuentas por período
static uint32_t es_tim_base(uint32_t tim, uint32_t clk, uint32_t hz)
{
    uint32_t div = clk / hz;
    uint32_t psc = (div - 1) / 65536U;  // el prescaler más chico que deja el ARR en 16 bits
    uint32_t per = div / (psc + 1);
    TIM_PSC(tim) = psc;
    TIM_ARR(tim) = per - 1;
    return per;
}

static void es_tim_arrancar(uint32_t tim)
{
    TIM_CR1(tim) = 1U << 7;   // ARPE
    TIM_EGR(tim) = 1U;        // UG: carga PSC, ARR y CCR
    TIM_CR1(tim) |= 1U;       // CEN
}

static void es_tim_parar(uint32_t tim)
{
    TIM_CR1(tim) = 0;
    TIM_CCER(tim) = 0;
    TIM_BDTR(tim) = 0;
}

static void es_tim4(void)  // DO1: TIM4_CH4
{
    RCC_APB1ENR1 |= RCC_TIM4EN;
    (void)RCC_APB1ENR1;
    TIM_CR1(TIM4_BASE) = 0;
    uint32_t per = es_tim_base(TIM4_BASE, PCLK1_HZ, es_hz[1]);
    TIM_CCR4(TIM4_BASE) = per * es_duty[1] / 100;
    TIM_CCMR2(TIM4_BASE) = (6U << 12) | (1U << 11);  // OC4M = PWM modo 1, OC4PE
    TIM_CCER(TIM4_BASE) = 1U << 12;                  // CC4E
    es_tim_arrancar(TIM4_BASE);
}

static void es_tim8(void)  // DO3: TIM8_CH1N, DO4: TIM8_CH3N
{
    RCC_APB2ENR |= RCC_TIM8EN;
    (void)RCC_APB2ENR;
    TIM_CR1(TIM8_BASE) = 0;
    uint32_t per = es_tim_base(TIM8_BASE, PCLK2_HZ, es_tim8_hz);
    uint32_t ccer = 0, ccmr1 = 0, ccmr2 = 0;
    if (es_pwm[3]) {
        TIM_CCR1(TIM8_BASE) = per * es_duty[3] / 100;
        ccmr1 = (6U << 4) | (1U << 3);  // OC1M = PWM modo 1, OC1PE
        ccer |= 1U << 2;                // CC1NE: solo la salida complementaria
    }
    if (es_pwm[4]) {
        TIM_CCR3(TIM8_BASE) = per * es_duty[4] / 100;
        ccmr2 = (6U << 4) | (1U << 3);  // OC3M = PWM modo 1, OC3PE
        ccer |= 1U << 10;               // CC3NE
    }
    TIM_CCMR1(TIM8_BASE) = ccmr1;
    TIM_CCMR2(TIM8_BASE) = ccmr2;
    TIM_CCER(TIM8_BASE) = ccer;
    TIM_BDTR(TIM8_BASE) = 1U << 15;     // MOE
    es_tim_arrancar(TIM8_BASE);
}

static void es_tim16(void)  // DO2: TIM16_CH1
{
    RCC_APB2ENR |= RCC_TIM16EN;
    (void)RCC_APB2ENR;
    TIM_CR1(TIM16_BASE) = 0;
    uint32_t per = es_tim_base(TIM16_BASE, PCLK2_HZ, es_hz[2]);
    TIM_CCR1(TIM16_BASE) = per * es_duty[2] / 100;
    TIM_CCMR1(TIM16_BASE) = (6U << 4) | (1U << 3);
    TIM_CCER(TIM16_BASE) = 1U << 0;     // CC1E
    TIM_BDTR(TIM16_BASE) = 1U << 15;    // MOE
    es_tim_arrancar(TIM16_BASE);
}

// Pone la salida n como quedó configurada en es_pwm / es_nivel / es_hz / es_duty
static void es_aplicar(uint32_t n)
{
    const es_pin_t *p = &es_do_pin[n];
    if (es_pwm[n]) {
        if (n == 1) {
            es_tim4();
            gpio_af(p->port, p->pin, 2);
        } else if (n == 2) {
            es_tim16();
            gpio_af(p->port, p->pin, 4);
        } else {
            es_tim8_hz = es_hz[n];
            es_tim8();
            gpio_af(p->port, p->pin, 4);
        }
    } else {
        gpio_salida(p->port, p->pin, (int)es_nivel[n]);  // primero el pin, después se apaga el timer
        if (n == 1) es_tim_parar(TIM4_BASE);
        else if (n == 2) es_tim_parar(TIM16_BASE);
        else if (!es_pwm[3] && !es_pwm[4]) es_tim_parar(TIM8_BASE);
        else es_tim8();
    }
}

// ---------------------------------------------------------------------------------------------
// DO2: se presta la línea TX de la consola

static void es_do2_soltar(void)
{
    if (!es_do2_tomada && !es_do2_pedida) return;
    es_do2_pedida = 0;
    if (es_do2_tomada) {
        es_do2_tomada = 0;
        es_pwm[2] = 0;
        es_tim_parar(TIM16_BASE);
        gpio_af(CONS_PORT, CONS_TX, 7);  // vuelve a ser el TX de la consola
        cputs("DO2 volvio a ser el TX de la consola\n");
    }
}

// Pide DO2 por "seg" segundos (nivel o PWM ya cargados en es_pwm[2] etc.)
static void es_do2_pedir(uint32_t seg)
{
    es_do2_seg = seg;
    es_do2_pedida = 1;
    cprintf("DO2 pasa a la prueba por %u s: mientras tanto la consola no imprime "
            "(los comandos se siguen recibiendo; \"parar\" la devuelve)\n", seg);
}

static void es_do2_step(void)
{
    if (es_do2_pedida && !es_do2_tomada && consola_tx_vacia()) {
        es_do2_tomada = 1;
        es_do2_desde = ticks;
        es_aplicar(2);
    }
    if (es_do2_tomada && (ticks - es_do2_desde) >= es_do2_seg * 1000U) es_do2_soltar();
}

// ---------------------------------------------------------------------------------------------
// Frecuencia en las entradas: EXTI cuenta flancos de subida

void EXTI1_IRQHandler(void)     { EXTI_PR1 = 1U << 1; es_cuenta[2]++; }   // DI2 = PE1
void EXTI2_IRQHandler(void)     { EXTI_PR1 = 1U << 2; es_cuenta[3]++; }   // DI3 = PD2
void EXTI9_5_IRQHandler(void)                                            // DI1 = PE5
{
    if (EXTI_PR1 & (1U << 5)) { EXTI_PR1 = 1U << 5; es_cuenta[1]++; }
}
void EXTI15_10_IRQHandler(void)                                          // DI4 = PC11
{
    if (EXTI_PR1 & (1U << 11)) { EXTI_PR1 = 1U << 11; es_cuenta[4]++; }
}

#define ES_EXTI_LINEAS ((1U << 1) | (1U << 2) | (1U << 5) | (1U << 11))

static void es_frec_activar(int on)
{
    if (on) {
        RCC_APB2ENR |= RCC_SYSCFGEN;
        (void)RCC_APB2ENR;
        // Puerto de cada línea: A = 0, B = 1, C = 2, D = 3, E = 4
        SYSCFG_EXTICR(0) = (SYSCFG_EXTICR(0) & ~((0xFU << 4) | (0xFU << 8))) | (4U << 4) | (3U << 8);
        SYSCFG_EXTICR(1) = (SYSCFG_EXTICR(1) & ~(0xFU << 4)) | (4U << 4);
        SYSCFG_EXTICR(2) = (SYSCFG_EXTICR(2) & ~(0xFU << 12)) | (2U << 12);
        EXTI_FTSR1 &= ~ES_EXTI_LINEAS;
        EXTI_RTSR1 |= ES_EXTI_LINEAS;
        EXTI_PR1 = ES_EXTI_LINEAS;
        EXTI_IMR1 |= ES_EXTI_LINEAS;
        for (uint32_t i = 1; i <= 4; i++) es_cuenta_ant[i] = es_cuenta[i];
        es_t_frec = ticks;
        nvic_habilitar(EXTI1_IRQn);
        nvic_habilitar(EXTI2_IRQn);
        nvic_habilitar(EXTI9_5_IRQn);
        nvic_habilitar(EXTI15_10_IRQn);
    } else {
        EXTI_IMR1 &= ~ES_EXTI_LINEAS;
        EXTI_RTSR1 &= ~ES_EXTI_LINEAS;
        nvic_deshabilitar(EXTI1_IRQn);
        nvic_deshabilitar(EXTI2_IRQn);
        nvic_deshabilitar(EXTI9_5_IRQn);
        nvic_deshabilitar(EXTI15_10_IRQn);
        for (uint32_t i = 1; i <= 4; i++) es_frec[i] = 0;
    }
    es_frec_on = on;
}

// ---------------------------------------------------------------------------------------------
// Lectura e informes

static uint32_t es_leer_di(void)  // bit 0 = DI1 ... bit 3 = DI4
{
    uint32_t v = 0;
    for (uint32_t i = 1; i <= 4; i++) v |= (uint32_t)gpio_leer(es_di_pin[i].port, es_di_pin[i].pin) << (i - 1);
    return v;
}

static void es_imprimir_di(uint32_t v)
{
    cprintf("DI1 %u  DI2 %u  DI3 %u  DI4 %u", v & 1U, (v >> 1) & 1U, (v >> 2) & 1U, (v >> 3) & 1U);
}

static void es_imprimir_do(void)
{
    for (uint32_t n = 1; n <= 4; n++) {
        cprintf("DO%u ", n);
        if (n == 2 && !es_do2_tomada) cputs("consola");
        else if (es_pwm[n]) cprintf("PWM %u Hz %u%%", es_hz[n], es_duty[n]);
        else cprintf("%u", es_nivel[n]);
        cputs(n < 4 ? "  " : "");
    }
}

static void es_imprimir_frec(void)
{
    for (uint32_t i = 1; i <= 4; i++) cprintf("DI%u %u Hz%s", i, es_frec[i], i < 4 ? "  " : "");
}

static void es_init(void)
{
    for (uint32_t n = 1; n <= 4; n++) {
        es_pwm[n] = es_nivel[n] = 0;
        es_hz[n] = 100000;
        es_duty[n] = 50;
    }
    es_do2_pedida = es_do2_tomada = 0;
    // DI2 (PE1) no se toca: es el RX de la consola en AF7, y en ese modo el IDR igual lee el pin
    for (uint32_t i = 1; i <= 4; i++) {
        if (i != 2) gpio_entrada(es_di_pin[i].port, es_di_pin[i].pin, 0);
    }
    es_di_ult = es_leer_di();
    es_cambios = 0;
    es_t_di = es_t_res = ticks;
    es_frec_on = 0;
}

// Lo que hace la prueba en cada vuelta; también lo usa la prueba integral
static void es_entradas_step(int informar)
{
    uint32_t v = es_leer_di();
    if (v != es_di_ult && (ticks - es_t_di) >= ES_DI_MIN_MS) {
        uint32_t cambio = v ^ es_di_ult;
        es_di_ult = v;
        es_t_di = ticks;
        es_cambios++;
        if (informar) {
            cputs("es: ");
            es_imprimir_di(v);
            cputs("  (cambio:");
            for (uint32_t i = 0; i < 4; i++) if (cambio & (1U << i)) cprintf(" DI%u", i + 1);
            cputs(")\n");
        }
    }
    if (es_frec_on && (ticks - es_t_frec) >= 1000U) {
        es_t_frec += 1000U;
        for (uint32_t i = 1; i <= 4; i++) {
            uint32_t c = es_cuenta[i];
            es_frec[i] = c - es_cuenta_ant[i];
            es_cuenta_ant[i] = c;
        }
    }
}

static void es_step(void)
{
    es_do2_step();
    es_entradas_step(1);
    if ((ticks - es_t_res) >= 1000U) {
        es_t_res += 1000U;
        if (resumen_activo && !es_do2_tomada) {
            cputs("es: ");
            es_imprimir_do();
            cputs(" | ");
            es_imprimir_di(es_di_ult);
            if (es_frec_on) {
                cputs(" | ");
                es_imprimir_frec();
            }
            cputs("\n");
        }
    }
}

static void es_deinit(void)
{
    es_do2_soltar();
    for (uint32_t n = 1; n <= 4; n++) es_pwm[n] = 0;
    es_tim_parar(TIM4_BASE);
    es_tim_parar(TIM8_BASE);
    es_tim_parar(TIM16_BASE);
    RCC_APB1ENR1 &= ~RCC_TIM4EN;
    RCC_APB2ENR &= ~(RCC_TIM8EN | RCC_TIM16EN);
    es_frec_activar(0);
}

static void es_estado(void)
{
    cputs("  ");
    es_imprimir_do();
    cputs("\n  ");
    es_imprimir_di(es_leer_di());
    cprintf("  (cambios %u)\n", es_cambios);
    if (es_frec_on) {
        cputs("  ");
        es_imprimir_frec();
        cputs("\n");
    }
}

static const prueba_t p_es = {
    .nombre = "es", .archivo = "ES",
    .descripcion = "Salidas en nivel o PWM y entradas con frecuencia (DIG-01/02/03): do, pwm, di, frec",
    .init = es_init, .step = es_step, .deinit = es_deinit, .estado = es_estado,
    .ok = &es_cambios,
};
