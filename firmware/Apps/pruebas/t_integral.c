// Prueba "integral": todas las interfaces a la vez (plan INT-01, INT-02, PRO-09 y EXT-06)
// - PWM de 100 kHz y 50 % en DO1, DO3 y DO4 (con "integral ... todas", también en DO2: la consola
//   no imprime mientras dura, pero sigue recibiendo comandos y "parar" la devuelve)
// - Lectura continua de las 4 entradas digitales
// - ADC continuo de las 4 entradas analógicas (promedio y VDDA por VREFINT, como "ai")
// - Tráfico CAN1 <-> CAN2 en los dos sentidos con el puente de J3, a la tasa de "can_tasa"
//   (0 = máxima: carga de bus alta para INT-02) y en el modo de "can_modo"
// - LIN como commander contra la otra placa, que tiene que correr lin_esclavo. Si esta placa es
//   MAESTRO y el enlace está activo, se lo pide sola antes de apagar el enlace
// Dura "integral_min" minutos (10 por defecto, 0 = sin fin) y al final imprime un informe con
// PASA / FALLA. Con "autoarranque integral" arranca sola al encender (arranques en frío de INT-01)

#define IG_ESPERA_MS 300U   // para que salga el pedido de lin_esclavo antes de apagar el enlace

static uint32_t ig_min = 10;
static uint32_t ig_hz = 100000;
static int ig_todas;

static int ig_fase, ig_resumen_guardado;
static uint32_t ig_t, ig_t_res, ig_cantidad_guardada, ig_t_led;
static int ig_led;
static volatile uint32_t ig_ok, ig_err;

static void ig_init(void)
{
    ig_fase = 0;
    ig_t = ticks;
    ig_ok = ig_err = 0;
    ig_resumen_guardado = resumen_activo;
    if (rol_actual == ROL_MAESTRO && enlace_activo) {
        enlace_pedir(ENL_CMD_CORRER, P_LIN_ESCLAVO, ACCION_NINGUNA);
        cputs("integral: le pido lin_esclavo a la otra placa por CAN2\n");
    } else {
        cputs("integral: la otra placa tiene que estar corriendo lin_esclavo\n");
    }
    cprintf("integral: %u min, PWM %u Hz en DO1 DO3 DO4%s, DI, ADC, CAN1<->CAN2 (%s) y LIN %u baud\n",
            ig_min, ig_hz, ig_todas ? " y DO2" : "", bc_fd ? "FD" : "clasico", lin_baud);
}

static void ig_arrancar(void)
{
    resumen_activo = 0;  // las partes no imprimen su resumen: lo hace la integral
    ig_cantidad_guardada = bc_cantidad;
    bc_cantidad = 0;     // tráfico sin fin mientras dure la integral

    es_init();
    uint32_t salidas[3] = { 1, 3, 4 };
    for (uint32_t k = 0; k < 3; k++) {
        uint32_t n = salidas[k];
        es_pwm[n] = 1;
        es_hz[n] = ig_hz;
        es_duty[n] = 50;
        es_aplicar(n);
    }
    if (ig_todas) {
        es_pwm[2] = 1;
        es_hz[2] = ig_hz;
        es_duty[2] = 50;
        es_do2_pedir(ig_min ? ig_min * 60U : ES_DO2_SEG_MAX);
    }

    ai_cargar_factores();
    adc_usar_vref = 1;
    adc_hw_init();

    enlace_suspender();
    can1_activar(bc_modo_fdcan());
    fdcan_iniciar(FDCAN2_BASE, bc_modo_fdcan());
    bc_motor_init("integral", 2, 0);
    bc_flujo(0, "CAN1->CAN2", FDCAN1_BASE, BC_ID_12, FDCAN2_BASE, BC_ID_12);
    bc_flujo(1, "CAN2->CAN1", FDCAN2_BASE, BC_ID_21, FDCAN1_BASE, BC_ID_21);

    lm_init();

    ig_fase = 1;
    ig_t = ig_t_res = ig_t_led = ticks;
}

static void ig_imprimir(void)
{
    uint32_t seg = (ticks - ig_t) / 1000U;
    cprintf("integral %u/%u s | PWM %u Hz | ", seg, ig_min * 60U, ig_hz);
    es_imprimir_di(es_di_ult);
    cprintf(" | ADC %u (VDDA %u) | CAN ok %u perd %u err %u | LIN ok %u err %u\n", adc_lecturas, adc_vdda_mv,
            bc_ok_total, bc_f[0].perdidas + bc_f[1].perdidas, bc_f[0].err + bc_f[1].err, lm_resp_ok, lm_resp_err);
}

static void ig_step(void)
{
    if (ig_fase == 0) {
        if ((ticks - ig_t) >= IG_ESPERA_MS) ig_arrancar();
        return;
    }

    es_do2_step();
    es_entradas_step(0);
    adc_medir_step();
    bc_motor_step();
    lm_step();

    ig_ok = bc_ok_total + lm_resp_ok;
    ig_err = bc_err_total + lm_resp_err;

    // LEDs: verde titila cada 500 ms si todo anda; rojo fijo si hubo algún error
    if ((ticks - ig_t_led) >= 500U) {
        ig_t_led = ticks;
        ig_led = !ig_led;
    }
    leds(ig_err != 0, ig_err == 0 && ig_led);

    if ((ticks - ig_t_res) >= 1000U) {
        ig_t_res += 1000U;
        if (ig_resumen_guardado && !es_do2_tomada) ig_imprimir();
    }
    if (ig_min && (ticks - ig_t) >= ig_min * 60000U) prueba_parar();
}

static void ig_informe(void)
{
    uint32_t seg = (ticks - ig_t) / 1000U;
    uint32_t ecr1 = FDCAN_ECR(FDCAN1_BASE), ecr2 = FDCAN_ECR(FDCAN2_BASE);
    int can_ok = 1;
    for (uint32_t i = 0; i < 2; i++) {
        if (bc_f[i].perdidas || bc_f[i].err || !bc_f[i].rx_ok) can_ok = 0;
    }
    if ((ecr1 & 0x7FFFU) || (ecr2 & 0x7FFFU) || bc_busoff1 || bc_busoff2) can_ok = 0;
    int lin_ok = lm_resp_ok && !lm_resp_err;
    int adc_ok = adc_lecturas != 0;

    cprintf("==== integral, %u s ====\n", seg);
    cprintf("  PWM: %u Hz, 50 %% en DO1 DO3 DO4%s\n", ig_hz, ig_todas ? " DO2" : "");
    cputs("  DI: ");
    es_imprimir_di(es_leer_di());
    cprintf(", cambios %u\n", es_cambios);
    cprintf("  ADC: %u tandas, VDDA %u mV, conector", adc_lecturas, adc_vdda_mv);
    for (uint32_t i = 0; i < 4; i++) {
        cprintf(" AN%u ", i + 1);
        ai_imprimir_v3(ai_mv_conector(i));
    }
    cputs(" V\n");
    for (uint32_t i = 0; i < 2; i++) {
        cprintf("  CAN %s: mandadas %u, recibidas %u, perdidas %u, con error %u\n", bc_f[i].nombre,
                bc_f[i].tx, bc_f[i].rx_ok, bc_f[i].perdidas, bc_f[i].err);
    }
    cprintf("  CAN1 TEC %u REC %u, CAN2 TEC %u REC %u, bus off %u / %u\n", ecr1 & 0xFFU, (ecr1 >> 8) & 0x7FU,
            ecr2 & 0xFFU, (ecr2 >> 8) & 0x7FU, bc_busoff1, bc_busoff2);
    cprintf("  LIN: respuestas OK %u, con error %u (sin respuesta %u, checksum %u, datos %u, eco %u, break %u)\n",
            lm_resp_ok, lm_resp_err, lm_e_sin_resp, lm_e_chk, lm_e_datos, lm_e_eco, lm_e_break);
    cprintf("  Resultado: %s (CAN %s, LIN %s, ADC %s)\n", can_ok && lin_ok && adc_ok ? "PASA" : "FALLA",
            can_ok ? "OK" : "FALLA", lin_ok ? "OK" : "FALLA", adc_ok ? "OK" : "FALLA");
}

static void ig_deinit(void)
{
    if (ig_fase == 1) {
        lin_hw_deinit();
        can1_apagar();
        enlace_reanudar();
        adc_hw_deinit();
        es_deinit();
        bc_cantidad = ig_cantidad_guardada;
    }
    resumen_activo = ig_resumen_guardado;
    if (ig_fase == 1) ig_informe();
}

static void ig_estado(void)
{
    if (ig_fase == 1) {
        cputs("  ");
        ig_imprimir();
    } else {
        cputs("  arrancando\n");
    }
}

static const prueba_t p_integral = {
    .nombre = "integral", .archivo = "Integral",
    .descripcion = "Todo a la vez: PWM, DI, ADC, CAN1<->CAN2 y LIN, N minutos (INT-01/02, PRO-09)",
    .init = ig_init, .step = ig_step, .deinit = ig_deinit, .estado = ig_estado,
    .ok = &ig_ok, .err = &ig_err,
    .flags = PRUEBA_SOLO_LOCAL,
};
