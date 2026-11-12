// Combo: varias funciones a la vez entre las dos placas (con "par combo" desde el MAESTRO)
// - ADC: cada placa mide sus entradas AN1..AN4 cada 100 ms
// - LIN (19200, checksum mejorado): cada placa le pasa a la otra sus 4 tensiones
//     ID 0x20: combo_maestro -> combo_esclavo, 8 bytes = AN1..AN4 en mV (uint16, little endian)
//     ID 0x21: combo_esclavo -> combo_maestro, 8 bytes = AN1..AN4 en mV
//   combo_maestro es el commander: cada 50 ms manda una trama, alternando 0x20 y 0x21
// - LEDs cruzados: cada placa muestra en sus LEDs la entrada de la OTRA placa elegida con "canal"
//   (por defecto AN1): menos de 1,1 V rojo, 1,1 a 2,2 V los dos, más de 2,2 V verde.
//   Si se corta el LIN (500 ms sin una trama buena), los dos LEDs titilan juntos cada 100 ms
// - Consola: cada 1 s (si "resumen on") las tensiones de las dos puntas y el estado del LIN.
//   La esclava lo manda por CAN2 y el maestro lo imprime por su UART con "[esclava]".
//   Avisos cuando se corta o vuelve el LIN y por cada falla; "traza on" muestra cada trama
// Hardware: el mismo cableado de LIN (J3-1 y GND entre placas, SW3 cerrado en el maestro) y algo
// en las entradas de J4 (por ejemplo un potenciómetro) para mover las tensiones

#define CB_ID_M2E       0x20U  // maestro -> esclava
#define CB_ID_E2M       0x21U  // esclava -> maestro
#define CB_LEN          8U
#define CB_PERIODO_MS   50U
#define CB_TIMEOUT_MS   500U
#define CB_TITILA_MS    100U
#define CB_RESUMEN_MS   1000U

static uint32_t cb_remoto[4];     // tensiones de la otra placa, en mV
static uint32_t cb_t_ok;          // última trama buena de la otra placa
static int cb_hay_remoto, cb_caido;
static limite_t cb_lim;
static uint32_t cb_t_res, cb_t_titila;
static int cb_titila;

static volatile uint32_t cb_tx, cb_ok, cb_err;  // tramas mandadas, recibidas bien y con error
static uint32_t cb_tx_res, cb_ok_res;

// AN1..AN4 en mV -> 8 bytes
static void cb_empaquetar(uint8_t *d)
{
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t mv = an_mv[i];
        d[2 * i] = (uint8_t)mv;
        d[2 * i + 1] = (uint8_t)(mv >> 8);
    }
}

static void cb_recibido(const uint8_t *d)
{
    for (uint32_t i = 0; i < 4; i++) cb_remoto[i] = d[2 * i] | ((uint32_t)d[2 * i + 1] << 8);
    cb_hay_remoto = 1;
    cb_t_ok = ticks;
    cb_ok++;
}

static void cb_init_comun(void)
{
    adc_hw_init();
    lin_hw_init();
    for (uint32_t i = 0; i < 4; i++) cb_remoto[i] = 0;
    cb_hay_remoto = 0;
    cb_caido = 0;
    cb_lim.usado = 0;
    cb_tx = cb_ok = cb_err = 0;
    cb_tx_res = cb_ok_res = 0;
    cb_t_ok = cb_t_res = cb_t_titila = ticks;
    cb_titila = 0;
    leds(0, 0);
}

// Lo que hacen las dos placas igual en cada step: medir, LEDs, avisos y resumen
static void cb_step_comun(const char *yo, const char *otro)
{
    adc_medir_step();

    int sin_lin = (ticks - cb_t_ok) > CB_TIMEOUT_MS;
    if (sin_lin && !cb_caido) {
        cb_caido = 1;
        cprintf("%s: no llegan datos de la otra placa por LIN hace 500 ms: revisar el cable LIN (J3-1) y GND, "
                "SW3 cerrado en el maestro y que la otra placa corra combo\n", yo);
    } else if (!sin_lin && cb_caido) {
        cb_caido = 0;
        cprintf("%s: volvieron los datos de la otra placa por LIN\n", yo);
    }

    // LEDs: la entrada elegida de la otra placa; si no hay LIN, titilan los dos
    if (sin_lin) {
        if ((ticks - cb_t_titila) >= CB_TITILA_MS) {
            cb_t_titila = ticks;
            cb_titila = !cb_titila;
            leds(cb_titila, cb_titila);
        }
    } else {
        adc_leds_mv(cb_remoto[adc_canal & 3U]);
    }

    if ((ticks - cb_t_res) >= CB_RESUMEN_MS) {
        cb_t_res += CB_RESUMEN_MS;
        if (resumen_activo) {
            uint32_t c = adc_canal & 3U;
            cprintf("%s: aca ", yo);
            adc_imprimir_cuatro(an_mv);
            cprintf(" | %s ", otro);
            if (cb_hay_remoto) adc_imprimir_cuatro(cb_remoto);
            else cputs("(sin datos)");
            cprintf(" | LIN tx %u/s rx OK %u/s err %u | LEDs: %s AN%u = %s\n",
                    cb_tx - cb_tx_res, cb_ok - cb_ok_res, cb_err, otro, c + 1,
                    sin_lin ? "sin LIN (titilan)" : adc_leds_texto(cb_remoto[c]));
        }
        cb_tx_res = cb_tx;
        cb_ok_res = cb_ok;
    }
}

static void cb_deinit(void)
{
    lin_hw_deinit();
    adc_hw_deinit();
}

static void cb_estado(void)
{
    cputs("  aca ");
    adc_imprimir_cuatro(an_mv);
    cputs(" | la otra placa ");
    if (cb_hay_remoto) adc_imprimir_cuatro(cb_remoto);
    else cputs("(sin datos)");
    cprintf(" | LEDs: la otra placa AN%u | eco err %u\n", (adc_canal & 3U) + 1, lin_eco_err);
}

// ---------------------------------------------------------------------------------------------
// combo_maestro: commander LIN

enum { CBM_ESPERA, CBM_BREAK, CBM_TX, CBM_RX };
static uint32_t cbm_estado, cbm_t0;
static int cbm_pide;  // 0 = toca mandar 0x20, 1 = toca pedir 0x21

static void cbm_init(void)
{
    cb_init_comun();
    cbm_estado = CBM_ESPERA;
    cbm_pide = 0;
    cbm_t0 = ticks;
}

static void cbm_fin(void)
{
    cbm_pide = !cbm_pide;
    cbm_estado = CBM_ESPERA;
}

static void cbm_step(void)
{
    int r;

    switch (cbm_estado) {
    case CBM_ESPERA:
        if ((ticks - cbm_t0) < CB_PERIODO_MS) break;
        cbm_t0 = ticks;
        lin_flush();
        USART_RQR(USART3_BASE) = RQR_SBKRQ;  // break
        lin.t = ticks;
        cbm_estado = CBM_BREAK;
        break;

    case CBM_BREAK:
        if ((USART_ISR(USART3_BASE) & ISR_LBDF) && !(USART_ISR(USART3_BASE) & ISR_SBKF)) {
            lin_flush();
            uint8_t pid = lin_pid(cbm_pide ? CB_ID_E2M : CB_ID_M2E);
            lin.tx[0] = LIN_SYNC;
            lin.tx[1] = pid;
            lin.ntx = 2;
            if (!cbm_pide) {  // 0x20: header, mis 4 tensiones y checksum
                cb_empaquetar(&lin.tx[2]);
                lin.tx[2 + CB_LEN] = lin_checksum(pid, &lin.tx[2], CB_LEN);
                lin.ntx = 3 + CB_LEN;
            }
            lin_empezar_tx();
            cbm_estado = CBM_TX;
        } else if ((ticks - lin.t) > LIN_BYTE_TIMEOUT_MS) {
            if (limite_ok(&cb_lim)) cputs("combo_maestro: no se pudo mandar el break (revisar EN = PD10 y el transceptor)\n");
            cb_err++;
            cbm_fin();
        }
        break;

    case CBM_TX:
        r = lin_tx_step();
        if (r < 0) {
            if (limite_ok(&cb_lim)) lin_explicar_eco("combo_maestro");
            cb_err++;
            cbm_fin();
        } else if (r > 0) {
            if (!cbm_pide) {
                cb_tx++;
                if (traza_activa) {
                    lin_traza(CB_ID_M2E, "tx", &lin.tx[2], CB_LEN);
                    cputs("\n");
                }
                cbm_fin();
            } else {
                lin_empezar_rx(CB_LEN + 1);  // la esclava completa con sus datos y el checksum
                cbm_estado = CBM_RX;
            }
        }
        break;

    default:  // CBM_RX
        r = lin_rx_step();
        if (r < 0) {
            if (!cb_caido && limite_ok(&cb_lim)) {
                cprintf("combo_maestro: la esclava no responde a 0x21 (llegaron %u de %u bytes)\n", lin.irx, CB_LEN + 1);
            }
            cb_err++;
            cbm_fin();
        } else if (r > 0) {
            uint8_t chk = lin_checksum(lin_pid(CB_ID_E2M), lin.rx, CB_LEN);
            int ok = lin.rx[CB_LEN] == chk;
            if (traza_activa) {
                lin_traza(CB_ID_E2M, "rx", lin.rx, CB_LEN);
                cputs(ok ? "  OK\n" : "  MAL\n");
            }
            if (ok) {
                cb_recibido(lin.rx);
            } else {
                cb_err++;
                if (limite_ok(&cb_lim)) {
                    cprintf("combo_maestro: trama 0x21 con checksum mal: llego 0x%02x, se esperaba 0x%02x\n",
                            lin.rx[CB_LEN], chk);
                }
            }
            cbm_fin();
        }
        break;
    }

    cb_step_comun("combo_maestro", "esclava");
}

static const prueba_t p_combo_maestro = {
    .nombre = "combo_maestro", .archivo = "Combo_Maestro",
    .descripcion = "ADC + LIN + LEDs cruzados: manda sus AN por LIN y muestra los de la otra placa",
    .init = cbm_init, .step = cbm_step, .deinit = cb_deinit, .estado = cb_estado,
    .ok = &cb_ok, .err = &cb_err,
};

// ---------------------------------------------------------------------------------------------
// combo_esclavo: responde al commander

enum { CBE_ESPERA, CBE_SYNC, CBE_PID, CBE_DATOS, CBE_RESPUESTA };
static uint32_t cbe_estado;

static void cbe_init(void)
{
    cb_init_comun();
    cbe_estado = CBE_ESPERA;
}

static void cbe_step(void)
{
    int r;

    switch (cbe_estado) {
    case CBE_ESPERA:
        if (USART_ISR(USART3_BASE) & ISR_LBDF) {
            lin_flush();  // limpia LBDF y descarta el 0x00 que deja el break
            lin_empezar_rx(1);
            cbe_estado = CBE_SYNC;
        }
        break;

    case CBE_SYNC:
        r = lin_rx_step();
        if (r < 0 || (r > 0 && lin.rx[0] != LIN_SYNC)) {
            cb_err++;
            if (limite_ok(&cb_lim)) cputs("combo_esclavo: break sin un sync valido detras\n");
            cbe_estado = CBE_ESPERA;
        } else if (r > 0) {
            lin_empezar_rx(1);
            cbe_estado = CBE_PID;
        }
        break;

    case CBE_PID:
        r = lin_rx_step();
        if (r < 0) {
            cbe_estado = CBE_ESPERA;
        } else if (r > 0) {
            if (lin.rx[0] == lin_pid(CB_ID_M2E)) {
                lin_empezar_rx(CB_LEN + 1);
                cbe_estado = CBE_DATOS;
            } else if (lin.rx[0] == lin_pid(CB_ID_E2M)) {
                // Completa el header del maestro con mis 4 tensiones
                cb_empaquetar(lin.tx);
                lin.tx[CB_LEN] = lin_checksum(lin_pid(CB_ID_E2M), lin.tx, CB_LEN);
                lin.ntx = CB_LEN + 1;
                lin_empezar_tx();
                cbe_estado = CBE_RESPUESTA;
            } else {
                cbe_estado = CBE_ESPERA;  // otro ID: se ignora
            }
        }
        break;

    case CBE_DATOS:
        r = lin_rx_step();
        if (r < 0) {
            cb_err++;
            if (!cb_caido && limite_ok(&cb_lim)) {
                cprintf("combo_esclavo: trama 0x20 incompleta (llegaron %u de %u bytes)\n", lin.irx, CB_LEN + 1);
            }
            cbe_estado = CBE_ESPERA;
        } else if (r > 0) {
            uint8_t chk = lin_checksum(lin_pid(CB_ID_M2E), lin.rx, CB_LEN);
            int ok = lin.rx[CB_LEN] == chk;
            if (traza_activa) {
                lin_traza(CB_ID_M2E, "rx", lin.rx, CB_LEN);
                cputs(ok ? "  OK\n" : "  MAL\n");
            }
            if (ok) {
                cb_recibido(lin.rx);
            } else {
                cb_err++;
                if (limite_ok(&cb_lim)) {
                    cprintf("combo_esclavo: trama 0x20 con checksum mal: llego 0x%02x, se esperaba 0x%02x\n",
                            lin.rx[CB_LEN], chk);
                }
            }
            cbe_estado = CBE_ESPERA;
        }
        break;

    default:  // CBE_RESPUESTA
        r = lin_tx_step();
        if (r != 0) {
            if (r > 0) {
                cb_tx++;
                if (traza_activa) {
                    lin_traza(CB_ID_E2M, "tx", lin.tx, CB_LEN);
                    cputs("\n");
                }
            } else {
                cb_err++;
                if (limite_ok(&cb_lim)) lin_explicar_eco("combo_esclavo");
            }
            cbe_estado = CBE_ESPERA;
        }
        break;
    }

    cb_step_comun("combo_esclavo", "maestro");
}

static const prueba_t p_combo_esclavo = {
    .nombre = "combo_esclavo", .archivo = "Combo_Esclavo",
    .descripcion = "ADC + LIN + LEDs cruzados: responde con sus AN por LIN y muestra los de la otra placa",
    .init = cbe_init, .step = cbe_step, .deinit = cb_deinit, .estado = cb_estado,
    .ok = &cb_ok, .err = &cb_err,
};
