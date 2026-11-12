// ADC: entradas AN1..AN4 con el ADC1
// - Cada 100 ms lee las 4 entradas (16 lecturas promediadas por canal) y guarda la tensión en an_mv[]
// - Una conversión por step(): no se queda esperando el EOC
// - Una vez por segundo (si "resumen on") imprime por la consola la tensión de las 4 entradas en voltios
//   (por ejemplo "AN1 1.10 V") y qué LEDs prende la entrada elegida
// - Los LEDs muestran la entrada elegida con "canal <1-4>":
//     menos de 1.1 V -> rojo, 1.1 V a 2.2 V -> rojo y verde, más de 2.2 V -> verde
// Entradas: AN1 = PB11 (IN14), AN2 = PA1 (IN2), AN3 = PC1 (IN7), AN4 = PA0 (IN1)
// La tensión es la del pin del ADC (0 a 3,3 V), después del divisor de cada entrada de J4:
// no es la tensión aplicada en la bornera
// El motor de medición (adc_hw_init, adc_medir_step, adc_hw_deinit) lo usa también la prueba combo

#define ADC_VDDA_MV      3300U  // referencia del ADC
#define ADC_PROMEDIO     16U
#define ADC_PERIODO_MS   100U
#define ADC_IMPRIMIR_MS  1000U
#define ADC_UMBRAL_BAJO  1100U  // mV
#define ADC_UMBRAL_ALTO  2200U  // mV

static const struct { uint32_t port; uint32_t pin; uint32_t canal; } adc_entradas[4] = {
    { GPIOB_BASE, 11, 14 },  // AN1 = PB11
    { GPIOA_BASE, 1,  2  },  // AN2 = PA1
    { GPIOC_BASE, 1,  7  },  // AN3 = PC1
    { GPIOA_BASE, 0,  1  },  // AN4 = PA0
};

// Para mirar desde el debugger (panel Watch)
volatile uint32_t an_raw[4];  // cuentas del ADC (0..4095)
volatile uint32_t an_mv[4];   // tensión en mV: an_mv[0] = AN1 ... an_mv[3] = AN4
static volatile uint32_t adc_lecturas;  // tandas de 4 entradas completas

static uint32_t adc_t, adc_t_imprimir;
static int adc_midiendo;
static uint32_t adc_i, adc_n, adc_suma;

// ---------------------------------------------------------------------------------------------
// Ayudas de presentación

// mV -> "1.10" (sin unidad)
static void adc_imprimir_num(uint32_t mv)
{
    uint32_t cv = (mv + 5) / 10;  // centésimas de volt, redondeado
    cprintf("%u.%02u", cv / 100, cv % 100);
}

// mV -> "1.10 V"
static void adc_imprimir_v(uint32_t mv)
{
    adc_imprimir_num(mv);
    cputs(" V");
}

// Las 4 entradas: "1.10 0.00 2.31 0.00 V"
static void adc_imprimir_cuatro(const volatile uint32_t *mv)
{
    for (uint32_t i = 0; i < 4; i++) {
        adc_imprimir_num(mv[i]);
        cputs(i < 3 ? " " : " V");
    }
}

static const char *adc_leds_texto(uint32_t mv)
{
    if (mv < ADC_UMBRAL_BAJO) return "rojo";
    if (mv > ADC_UMBRAL_ALTO) return "verde";
    return "rojo y verde";
}

// LEDs según la tensión: < 1,1 V rojo, 1,1 a 2,2 V los dos, > 2,2 V verde
static void adc_leds_mv(uint32_t mv)
{
    leds(mv <= ADC_UMBRAL_ALTO, mv >= ADC_UMBRAL_BAJO);
}

// AN1 1.10 V  AN2 0.00 V  AN3 2.31 V  AN4 0.00 V  | LEDs (AN1): rojo y verde
static void adc_imprimir(void)
{
    for (uint32_t i = 0; i < 4; i++) {
        cprintf("AN%u ", i + 1);
        adc_imprimir_v(an_mv[i]);
        cputs("  ");
    }
    uint32_t c = adc_canal & 3U;
    cprintf("| LEDs (AN%u): %s\n", c + 1, adc_leds_texto(an_mv[c]));
}

// ---------------------------------------------------------------------------------------------
// Motor de medición

static void adc_convertir(uint32_t canal)
{
    ADC1_SQR1 = canal << 6;  // una sola conversión (L = 0) del canal pedido
    ADC1_CR |= ADC_CR_ADSTART;
}

static void adc_hw_init(void)
{
    RCC_AHB2ENR |= RCC_ADC12EN;
    (void)RCC_AHB2ENR;

    for (uint32_t i = 0; i < 4; i++) gpio_analogico(adc_entradas[i].port, adc_entradas[i].pin);

    // Reloj del ADC: HCLK / 1 (sincrónico), así no hace falta configurar el PLL
    ADC12_CCR = (ADC12_CCR & ~(3U << 16)) | (1U << 16);

    // Sale de deep power-down, prende el regulador y espera su arranque (20 us)
    ADC1_CR &= ~ADC_CR_DEEPPWD;
    ADC1_CR |= ADC_CR_ADVREGEN;
    for (volatile uint32_t i = 0; i < 1000; i++);

    // Calibración en modo single-ended (unos pocos us)
    ADC1_CR |= ADC_CR_ADCAL;
    while (ADC1_CR & ADC_CR_ADCAL);

    // Muestreo largo (640.5 ciclos) en todos los canales: sirve para fuentes de alta impedancia
    ADC1_SMPR1 = 0x3FFFFFFFU;
    ADC1_SMPR2 = 0x07FFFFFFU;

    ADC1_ISR = ADC_ISR_ADRDY;  // se limpia escribiendo 1
    ADC1_CR |= ADC_CR_ADEN;
    while (!(ADC1_ISR & ADC_ISR_ADRDY));

    for (uint32_t i = 0; i < 4; i++) an_raw[i] = an_mv[i] = 0;
    adc_midiendo = 0;
    adc_lecturas = 0;
    adc_t = ticks - ADC_PERIODO_MS;  // la primera tanda arranca enseguida
}

// Avanza una conversión. Devuelve 1 cuando terminó una tanda de las 4 entradas (an_mv[] actualizado)
static int adc_medir_step(void)
{
    if (!adc_midiendo) {
        if ((ticks - adc_t) >= ADC_PERIODO_MS) {
            adc_t = ticks;
            adc_midiendo = 1;
            adc_i = adc_n = adc_suma = 0;
            adc_convertir(adc_entradas[0].canal);
        }
    } else if (ADC1_ISR & ADC_ISR_EOC) {
        adc_suma += ADC1_DR;  // leer DR limpia EOC
        if (++adc_n == ADC_PROMEDIO) {
            an_raw[adc_i] = adc_suma / ADC_PROMEDIO;
            an_mv[adc_i] = an_raw[adc_i] * ADC_VDDA_MV / 4095;
            adc_n = adc_suma = 0;
            adc_i++;
        }
        if (adc_i < 4) {
            adc_convertir(adc_entradas[adc_i].canal);
        } else {
            adc_midiendo = 0;
            adc_lecturas++;
            return 1;
        }
    }
    return 0;
}

static void adc_hw_deinit(void)
{
    if (ADC1_CR & ADC_CR_ADSTART) {
        ADC1_CR |= ADC_CR_ADSTP;
        while (ADC1_CR & ADC_CR_ADSTP);
    }
    if (ADC1_CR & ADC_CR_ADEN) {
        ADC1_CR |= ADC_CR_ADDIS;
        while (ADC1_CR & ADC_CR_ADEN);
    }
    ADC1_CR = 0;               // apaga el regulador
    ADC1_CR = ADC_CR_DEEPPWD;  // deep power-down, como al reset
    RCC_AHB2ENR &= ~RCC_ADC12EN;
}

// ---------------------------------------------------------------------------------------------
// Prueba adc

static void adc_init(void)
{
    adc_hw_init();
    adc_t_imprimir = ticks;
}

static void adc_step(void)
{
    if (adc_medir_step()) adc_leds_mv(an_mv[adc_canal & 3U]);

    if ((ticks - adc_t_imprimir) >= ADC_IMPRIMIR_MS) {
        adc_t_imprimir += ADC_IMPRIMIR_MS;
        if (resumen_activo) {
            cputs("adc: ");
            adc_imprimir();
        }
    }
}

static void adc_deinit(void)
{
    adc_hw_deinit();
}

static void adc_estado(void)
{
    cputs("  ");
    adc_imprimir();
}

static const prueba_t p_adc = {
    .nombre = "adc", .archivo = "ADC",
    .descripcion = "AN1-AN4 cada 100 ms, imprime los voltios cada 1 s; LEDs segun \"canal\"",
    .init = adc_init, .step = adc_step, .deinit = adc_deinit, .estado = adc_estado,
    .ok = &adc_lecturas,
};
