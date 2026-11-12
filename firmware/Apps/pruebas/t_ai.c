// Prueba "ai": entradas analógicas en la tensión del conector (plan AI-02)
// - Usa el motor del ADC con N muestras promediadas por canal ("promedio", 256 por defecto) y VDDA
//   medida con VREFINT en cada tanda, así la lectura no depende de que el LDO dé justo 3,3 V
// - Tensión en el conector = tensión en el pin x factor del divisor. El factor de cada canal es
//   (R serie + R a GND) / R a GND: se carga con "divisor <canal> <R serie> <R a GND>" (en ohm) y
//   queda guardado en la flash. Sin cargar, se usa el nominal: fondo de escala / 3,3 V
// - Cada 1 s imprime los 4 canales en el conector y en % del fondo de escala
// - "ref <canal> <mV>" compara con el multímetro y da el error en % FS (criterio: <= 1 %)

#define AI_PARAM_FACTOR  1U     // claves 1..4 de la flash: factor x1000 de AN1..AN4

static const uint32_t ai_fs_mv[4] = { 3300, 5000, 12000, 24000 };  // fondo de escala de cada canal
static uint32_t ai_factor[4];   // V conector / V pin, x1000
static uint32_t ai_t_res;

static void ai_cargar_factores(void)
{
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t v = param_leer(AI_PARAM_FACTOR + i);
        ai_factor[i] = (v != PARAM_VACIO && v >= 1000U) ? v : ai_fs_mv[i] * 1000U / 3300U;
    }
}

static uint32_t ai_mv_conector(uint32_t i)
{
    return (uint32_t)(((uint64_t)an_mv[i] * ai_factor[i]) / 1000U);
}

// mV -> "12.034"
static void ai_imprimir_v3(uint32_t mv)
{
    cprintf("%u.%03u", mv / 1000U, mv % 1000U);
}

// "%" con un decimal: 503 -> "50.3"
static void ai_imprimir_pct(uint32_t mv, uint32_t fs)
{
    uint32_t p = (uint32_t)(((uint64_t)mv * 1000U + fs / 2) / fs);
    cprintf("%u.%u", p / 10U, p % 10U);
}

static void ai_imprimir(void)
{
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t mv = ai_mv_conector(i);
        cprintf("AN%u ", i + 1);
        ai_imprimir_v3(mv);
        cputs(" V (");
        ai_imprimir_pct(mv, ai_fs_mv[i]);
        cputs(" %)  ");
    }
    cprintf("| VDDA %u mV, prom %u\n", adc_vdda_mv, adc_promedio);
}

static void ai_init(void)
{
    ai_cargar_factores();
    adc_usar_vref = 1;
    adc_hw_init();
    ai_t_res = ticks;
}

static void ai_step(void)
{
    adc_medir_step();
    if ((ticks - ai_t_res) >= 1000U) {
        ai_t_res += 1000U;
        if (resumen_activo && adc_lecturas) {
            cputs("ai: ");
            ai_imprimir();
        }
    }
}

static void ai_deinit(void)
{
    adc_hw_deinit();
}

static void ai_estado(void)
{
    cputs("  ");
    ai_imprimir();
    for (uint32_t i = 0; i < 4; i++) {
        cprintf("  AN%u: pin %u mV, factor %u.%03u, fondo de escala %u mV\n", i + 1, an_mv[i],
                ai_factor[i] / 1000U, ai_factor[i] % 1000U, ai_fs_mv[i]);
    }
}

static const prueba_t p_ai = {
    .nombre = "ai", .archivo = "AI",
    .descripcion = "AN1-AN4 en voltios del conector, promedio de N, VDDA por VREFINT (AI-02): ref, divisor",
    .init = ai_init, .step = ai_step, .deinit = ai_deinit, .estado = ai_estado,
    .ok = &adc_lecturas,
};
