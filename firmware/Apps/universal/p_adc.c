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
static uint32_t adc_sumas[5];

// Opciones del motor (Universal.c las ajusta al arrancar y con comandos)
static uint32_t adc_promedio = ADC_PROMEDIO;  // lecturas promediadas por canal (1 a 256)
static int adc_usar_vref;                     // 1: mide VREFINT en cada tanda y corrige VDDA
static uint32_t adc_vdda_mv = ADC_VDDA_MV;    // VDDA usada para pasar a mV (medida si adc_usar_vref)
// Convertidor que mide AN1..AN4: 1 = ADC1 (por defecto), 2 = ADC2 (comando adc_conv).
// VREFINT solo está conectado al ADC1: la VDDA se mide siempre con el ADC1
static uint32_t adc_conv = 1;
volatile uint32_t an_vref_raw;                // cuentas de VREFINT (promedio)

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
    cprintf("| LEDs (AN%u): %s | ADC%u\n", c + 1, adc_leds_texto(an_mv[c]), adc_conv);
}

// ---------------------------------------------------------------------------------------------
// Motor de medición

static uint32_t adc_activos;  // bit 0 = ADC1 prendido, bit 1 = ADC2 prendido

static uint32_t adc_base_entradas(void) { return adc_conv == 2 ? ADC2_BASE : ADC1_BASE; }

// Canal y convertidor de la medición número i de una tanda (0..3 = AN1..AN4, 4 = VREFINT)
static uint32_t adc_canal_hw(uint32_t i)
{
    return i < 4 ? adc_entradas[i].canal : ADC_CANAL_VREFINT;
}

static uint32_t adc_base_hw(uint32_t i)
{
    return i < 4 ? adc_base_entradas() : ADC1_BASE;
}

static void adc_convertir(uint32_t i)
{
    uint32_t b = adc_base_hw(i);
    ADC_SQR1(b) = adc_canal_hw(i) << 6;  // una sola conversión (L = 0) del canal pedido
    ADC_CR(b) |= ADC_CR_ADSTART;
}

// Prende un convertidor: regulador, calibración, muestreo largo y enable
static void adc_prender(uint32_t b)
{
    // Sale de deep power-down, prende el regulador y espera su arranque (20 us)
    ADC_CR(b) &= ~ADC_CR_DEEPPWD;
    ADC_CR(b) |= ADC_CR_ADVREGEN;
    for (volatile uint32_t i = 0; i < 1000; i++);

    // Calibración en modo single-ended (unos pocos us)
    ADC_CR(b) |= ADC_CR_ADCAL;
    while (ADC_CR(b) & ADC_CR_ADCAL);

    // Muestreo largo (640.5 ciclos) en todos los canales: sirve para fuentes de alta impedancia
    ADC_SMPR1(b) = 0x3FFFFFFFU;
    ADC_SMPR2(b) = 0x07FFFFFFU;

    ADC_ISR(b) = ADC_ISR_ADRDY;  // se limpia escribiendo 1
    ADC_CR(b) |= ADC_CR_ADEN;
    while (!(ADC_ISR(b) & ADC_ISR_ADRDY));
}

static void adc_apagar(uint32_t b)
{
    if (ADC_CR(b) & ADC_CR_ADSTART) {
        ADC_CR(b) |= ADC_CR_ADSTP;
        while (ADC_CR(b) & ADC_CR_ADSTP);
    }
    if (ADC_CR(b) & ADC_CR_ADEN) {
        ADC_CR(b) |= ADC_CR_ADDIS;
        while (ADC_CR(b) & ADC_CR_ADEN);
    }
    ADC_CR(b) = 0;               // apaga el regulador
    ADC_CR(b) = ADC_CR_DEEPPWD;  // deep power-down, como al reset
}

static void adc_hw_init(void)
{
    RCC_AHB2ENR |= RCC_ADC12EN;
    (void)RCC_AHB2ENR;

    for (uint32_t i = 0; i < 4; i++) gpio_analogico(adc_entradas[i].port, adc_entradas[i].pin);

    // Reloj del ADC: HCLK / 1 (sincrónico), así no hace falta configurar el PLL.
    // CCR es común a ADC1 y ADC2: se escribe con los dos apagados
    ADC12_CCR = (ADC12_CCR & ~(3U << 16)) | (1U << 16);
    if (adc_usar_vref) ADC12_CCR |= ADC_CCR_VREFEN;
    else ADC12_CCR &= ~ADC_CCR_VREFEN;
    if (adc_promedio < 1) adc_promedio = 1;
    if (adc_promedio > 256) adc_promedio = 256;
    if (adc_conv != 2) adc_conv = 1;

    adc_activos = 0;
    if (adc_conv == 1 || adc_usar_vref) {
        adc_prender(ADC1_BASE);
        adc_activos |= 1U;
    }
    if (adc_conv == 2) {
        adc_prender(ADC2_BASE);
        adc_activos |= 2U;
    }

    for (uint32_t i = 0; i < 4; i++) an_raw[i] = an_mv[i] = 0;
    adc_midiendo = 0;
    adc_lecturas = 0;
    adc_t = ticks - ADC_PERIODO_MS;  // la primera tanda arranca enseguida
}

// Avanza una conversión. Devuelve 1 cuando terminó una tanda de las 4 entradas (an_mv[] actualizado)
static int adc_medir_step(void)
{
    uint32_t total = adc_usar_vref ? 5U : 4U;

    if (!adc_midiendo) {
        if ((ticks - adc_t) >= ADC_PERIODO_MS) {
            adc_t = ticks;
            adc_midiendo = 1;
            adc_i = adc_n = adc_suma = 0;
            adc_convertir(0);
        }
    } else if (ADC_ISR(adc_base_hw(adc_i)) & ADC_ISR_EOC) {
        adc_suma += ADC_DR(adc_base_hw(adc_i));  // leer DR limpia EOC
        if (++adc_n >= adc_promedio) {
            adc_sumas[adc_i] = adc_suma;
            adc_n = adc_suma = 0;
            adc_i++;
        }
        if (adc_i < total) {
            adc_convertir(adc_i);
        } else {
            adc_midiendo = 0;
            // VDDA real a partir de VREFINT: VDDA = 3,0 V * VREFINT_CAL / lectura
            if (adc_usar_vref && adc_sumas[4]) {
                an_vref_raw = adc_sumas[4] / adc_promedio;
                adc_vdda_mv = (uint32_t)((3000ULL * VREFINT_CAL * adc_promedio) / adc_sumas[4]);
            }
            for (uint32_t i = 0; i < 4; i++) {
                an_raw[i] = adc_sumas[i] / adc_promedio;
                an_mv[i] = (uint32_t)(((uint64_t)adc_sumas[i] * adc_vdda_mv) / (4095ULL * adc_promedio));
            }
            adc_lecturas++;
            return 1;
        }
    }
    return 0;
}

static void adc_hw_deinit(void)
{
    if (adc_activos & 1U) adc_apagar(ADC1_BASE);
    if (adc_activos & 2U) adc_apagar(ADC2_BASE);
    adc_activos = 0;
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
