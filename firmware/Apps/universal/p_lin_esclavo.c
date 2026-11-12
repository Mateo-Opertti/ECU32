// LIN_Esclavo (la otra placa corre LIN_Maestro)
// - ID 0x10: recibe los 4 bytes del maestro (contador, contador+1, ...) y los verifica
// - ID 0x11: responde [último contador, ~contador, 0xA5, 0x5A] para que el maestro lo verifique
// LEDs: el verde (PF10) alterna con cada trama 0x10 correcta,
//       el rojo (PF9) se prende si llega una trama mal o pasan 500 ms sin una buena

enum { LE_ESPERA, LE_SYNC, LE_PID, LE_DATOS, LE_RESPUESTA };

static volatile uint32_t le_rx_ok;      // tramas 0x10 recibidas bien
static volatile uint32_t le_rx_err;     // tramas que llegaron mal (sync, datos o checksum)
static volatile uint32_t le_resp_count; // respuestas 0x11 enviadas

static uint32_t le_estado, le_t_ok;
static uint8_t le_ultimo;
static int le_error;

static void le_init(void)
{
    lin_hw_init();
    le_rx_ok = le_rx_err = le_resp_count = 0;
    le_estado = LE_ESPERA;
    le_ultimo = 0;
    le_error = 0;
    le_t_ok = ticks;
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
        if (r < 0 || (r > 0 && lin.rx[0] != LIN_SYNC)) {
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
            le_mal();
        } else if (r > 0) {
            const uint8_t *d = lin.rx;
            int ok = d[LIN_LEN] == lin_checksum(lin_pid(LIN_ID_MAESTRO), d, LIN_LEN);
            for (uint32_t i = 1; ok && i < LIN_LEN; i++) {
                if (d[i] != (uint8_t)(d[0] + i)) ok = 0;
            }
            if (ok) {
                le_ultimo = d[0];
                le_rx_ok++;
                le_t_ok = ticks;
                le_error = 0;
                led_alternar(LED_VERDE);
                le_estado = LE_ESPERA;
            } else {
                le_mal();
            }
        }
        break;

    default:  // LE_RESPUESTA
        r = lin_tx_step();
        if (r != 0) {
            if (r > 0) le_resp_count++;
            le_estado = LE_ESPERA;
        }
        break;
    }

    if (le_error || (ticks - le_t_ok) > LIN_RX_TIMEOUT_MS) leds(1, 0);
    else gpio_poner(LED_PORT, LED_ROJO, 0);
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
