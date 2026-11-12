// LIN_Esclavo (la otra placa corre LIN_Maestro)
// - ID 0x10: recibe los 4 bytes del maestro (contador, contador+1, ...) y los verifica
// - ID 0x11: responde [último contador, ~contador, 0xA5, 0x5A] para que el maestro lo verifique
// LEDs: el verde (PF10) alterna con cada trama 0x10 correcta,
//       el rojo (PF9) se prende si llega una trama mal o pasan 500 ms sin una buena
//
// Consola:
// - cada 1 s (si "resumen on"): tramas 0x10 buenas por segundo, último contador,
//   respuestas 0x11 por segundo, errores y errores de eco
// - en el momento: el maestro deja de mandar o vuelve, y el motivo de cada falla
//   (sync, trama incompleta, checksum, datos o eco de la respuesta)
// - con "traza on": cada trama 0x10 recibida y cada respuesta 0x11

#define LE_RESUMEN_MS 1000U

enum { LE_ESPERA, LE_SYNC, LE_PID, LE_DATOS, LE_RESPUESTA };

static volatile uint32_t le_rx_ok;      // tramas 0x10 recibidas bien
static volatile uint32_t le_rx_err;     // tramas que llegaron mal (sync, datos o checksum)
static volatile uint32_t le_resp_count; // respuestas 0x11 enviadas

static uint32_t le_estado, le_t_ok;
static uint8_t le_ultimo;
static int le_error;

// Consola
static limite_t le_lim;
static int le_caido;
static uint32_t le_t_res, le_ok_res, le_resp_res;

static void le_init(void)
{
    lin_hw_init();
    le_rx_ok = le_rx_err = le_resp_count = 0;
    le_estado = LE_ESPERA;
    le_ultimo = 0;
    le_error = 0;
    le_t_ok = ticks;

    le_lim.usado = 0;
    le_caido = 0;
    le_t_res = ticks;
    le_ok_res = le_resp_res = 0;
}

static void le_mal(void)
{
    le_rx_err++;
    le_error = 1;
    le_estado = LE_ESPERA;
}

static void le_step(void)
{
    int r;

    switch (le_estado) {
    case LE_ESPERA:
        // Cada trama arranca con un break: hasta que no llega uno, no hay nada que hacer
        if (USART_ISR(USART3_BASE) & ISR_LBDF) {
            lin_flush();  // limpia LBDF y descarta el 0x00 que deja el break
            lin_empezar_rx(1);
            le_estado = LE_SYNC;
        }
        break;

    case LE_SYNC:
        r = lin_rx_step();
        if (r < 0) {
            if (limite_ok(&le_lim)) cputs("lin_esclavo: llego un break sin sync detras\n");
            le_mal();
        } else if (r > 0 && lin.rx[0] != LIN_SYNC) {
            if (limite_ok(&le_lim)) {
                cprintf("lin_esclavo: sync invalido: llego 0x%02x, se esperaba 0x55 (bit rate distinto o ruido)\n",
                        lin.rx[0]);
            }
            le_mal();
        } else if (r > 0) {
            lin_empezar_rx(1);
            le_estado = LE_PID;
        }
        break;

    case LE_PID:
        r = lin_rx_step();
        if (r < 0) {
            le_estado = LE_ESPERA;
        } else if (r > 0) {
            if (lin.rx[0] == lin_pid(LIN_ID_MAESTRO)) {
                lin_empezar_rx(LIN_LEN + 1);
                le_estado = LE_DATOS;
            } else if (lin.rx[0] == lin_pid(LIN_ID_ESCLAVO)) {
                // Completa el header del maestro con la respuesta
                lin.tx[0] = le_ultimo;
                lin.tx[1] = (uint8_t)~le_ultimo;
                lin.tx[2] = 0xA5;
                lin.tx[3] = 0x5A;
                lin.tx[LIN_LEN] = lin_checksum(lin_pid(LIN_ID_ESCLAVO), lin.tx, LIN_LEN);
                lin.ntx = LIN_LEN + 1;
                lin_empezar_tx();
                le_estado = LE_RESPUESTA;
            } else {
                le_estado = LE_ESPERA;  // trama de otro ID: se ignora
            }
        }
        break;

    case LE_DATOS:
        r = lin_rx_step();
        if (r < 0) {
            if (limite_ok(&le_lim)) {
                cprintf("lin_esclavo: trama 0x10 incompleta: llegaron %u de %u bytes\n", lin.irx, LIN_LEN + 1);
            }
            le_mal();
        } else if (r > 0) {
            const uint8_t *d = lin.rx;
            uint8_t chk = lin_checksum(lin_pid(LIN_ID_MAESTRO), d, LIN_LEN);
            int chk_ok = d[LIN_LEN] == chk;
            int datos_ok = 1;
            for (uint32_t i = 1; i < LIN_LEN; i++) {
                if (d[i] != (uint8_t)(d[0] + i)) datos_ok = 0;
            }
            if (traza_activa) {
                lin_traza(LIN_ID_MAESTRO, "rx", d, LIN_LEN);
                cputs(chk_ok && datos_ok ? "  OK\n" : "  MAL\n");
            }
            if (chk_ok && datos_ok) {
                le_ultimo = d[0];
                le_rx_ok++;
                le_t_ok = ticks;
                le_error = 0;
                led_alternar(LED_VERDE);
                if (le_caido) {
                    cputs("lin_esclavo: el maestro volvio\n");
                    le_caido = 0;
                }
                le_estado = LE_ESPERA;
            } else {
                if (limite_ok(&le_lim)) {
                    if (!chk_ok) {
                        cprintf("lin_esclavo: trama 0x10 con checksum mal: llego 0x%02x, se esperaba 0x%02x\n",
                                d[LIN_LEN], chk);
                    } else {
                        cputs("lin_esclavo: trama 0x10 con datos mal:");
                        cput_bytes(d, LIN_LEN);
                        cputs(" (tienen que ir subiendo de a 1)\n");
                    }
                }
                le_mal();
            }
        }
        break;

    default:  // LE_RESPUESTA
        r = lin_tx_step();
        if (r != 0) {
            if (r > 0) {
                le_resp_count++;
                if (traza_activa) {
                    lin_traza(LIN_ID_ESCLAVO, "tx", lin.tx, LIN_LEN);
                    cputs("\n");
                }
            } else if (limite_ok(&le_lim)) {
                lin_explicar_eco("lin_esclavo");
            }
            le_estado = LE_ESPERA;
        }
        break;
    }

    if (!le_caido && (ticks - le_t_ok) > LIN_RX_TIMEOUT_MS) {
        le_caido = 1;
        cputs("lin_esclavo: sin tramas 0x10 buenas del maestro hace 500 ms: revisar el cable LIN (J3-1) y GND, "
              "SW3 cerrado en el maestro y que la otra placa corra lin_maestro\n");
    }

    if (le_error || (ticks - le_t_ok) > LIN_RX_TIMEOUT_MS) leds(1, 0);
    else gpio_poner(LED_PORT, LED_ROJO, 0);

    if ((ticks - le_t_res) >= LE_RESUMEN_MS) {
        le_t_res += LE_RESUMEN_MS;
        if (resumen_activo) {
            cprintf("lin_esclavo: 0x10 OK %u/s (contador %u)  0x11 respondidas %u/s  err %u total | eco err %u\n",
                    le_rx_ok - le_ok_res, le_ultimo, le_resp_count - le_resp_res, le_rx_err, lin_eco_err);
        }
        le_ok_res = le_rx_ok;
        le_resp_res = le_resp_count;
    }
}

static void le_deinit(void)
{
    lin_hw_deinit();
}

static void le_estado_imprimir(void)
{
    cprintf("  respuestas 0x11 enviadas %u, errores de eco %u\n", le_resp_count, lin_eco_err);
}

static const prueba_t p_lin_esclavo = {
    .nombre = "lin_esclavo", .archivo = "LIN_Esclavo",
    .descripcion = "LIN 19200 entre placas: recibe 0x10 y responde 0x11",
    .init = le_init, .step = le_step, .deinit = le_deinit, .estado = le_estado_imprimir,
    .ok = &le_rx_ok, .err = &le_rx_err,
};
