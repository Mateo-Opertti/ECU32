// LIN_Maestro (la otra placa corre LIN_Esclavo)
// Cada 50 ms alterna entre dos tramas:
// - ID 0x10: el maestro manda 4 bytes con un contador (los recibe el esclavo)
// - ID 0x11: el maestro manda solo el header y el esclavo responde 4 bytes
//   [contador, ~contador, 0xA5, 0x5A] con el último contador que recibió
// LEDs: el verde (PF10) alterna con cada respuesta correcta del esclavo,
//       el rojo (PF9) se prende si la respuesta llega mal o pasan 500 ms sin una buena
// En la placa maestra va SW3 cerrado (pull-up de master LIN)

#define LM_PERIODO_MS 50U

enum { LM_ESPERA, LM_BREAK, LM_TX, LM_RX };

static volatile uint32_t lm_tx_count;  // tramas 0x10 enviadas
static volatile uint32_t lm_resp_ok;   // respuestas 0x11 del esclavo correctas
static volatile uint32_t lm_resp_err;  // respuestas que no llegaron o llegaron mal

static uint32_t lm_estado, lm_t0, lm_t_ok;
static uint8_t lm_cont;
static int lm_toca_respuesta, lm_error;

static void lm_init(void)
{
    lin_hw_init();
    lm_tx_count = lm_resp_ok = lm_resp_err = 0;
    lm_estado = LM_ESPERA;
    lm_cont = 0;
    lm_toca_respuesta = 0;
    lm_error = 0;
    lm_t0 = lm_t_ok = ticks;
}

// Fin de una trama. ok solo importa en la 0x11
static void lm_fin(int ok)
{
    if (lm_toca_respuesta) {
        if (ok) {
            lm_resp_ok++;
            lm_t_ok = ticks;
            lm_error = 0;
            led_alternar(LED_VERDE);
        } else {
            lm_resp_err++;
            lm_error = 1;
        }
        lm_cont++;
    }
    lm_toca_respuesta = !lm_toca_respuesta;
    lm_estado = LM_ESPERA;
}

static void lm_step(void)
{
    int r;

    switch (lm_estado) {
    case LM_ESPERA:
        if ((ticks - lm_t0) < LM_PERIODO_MS) break;
        lm_t0 = ticks;
        lin_flush();
        USART_RQR(USART3_BASE) = RQR_SBKRQ;  // break
        lin.t = ticks;
        lm_estado = LM_BREAK;
        break;

    case LM_BREAK:
        if ((USART_ISR(USART3_BASE) & ISR_LBDF) && !(USART_ISR(USART3_BASE) & ISR_SBKF)) {
            lin_flush();  // descarta el 0x00 con error de framing que deja el break
            uint8_t id = lm_toca_respuesta ? LIN_ID_ESCLAVO : LIN_ID_MAESTRO;
            uint8_t pid = lin_pid(id);
            lin.tx[0] = LIN_SYNC;
            lin.tx[1] = pid;
            lin.ntx = 2;
            if (!lm_toca_respuesta) {  // trama 0x10: header, datos y checksum
                for (uint32_t i = 0; i < LIN_LEN; i++) lin.tx[2 + i] = (uint8_t)(lm_cont + i);
                lin.tx[2 + LIN_LEN] = lin_checksum(pid, &lin.tx[2], LIN_LEN);
                lin.ntx = 3 + LIN_LEN;
            }
            lin_empezar_tx();
            lm_estado = LM_TX;
        } else if ((ticks - lin.t) > LIN_BYTE_TIMEOUT_MS) {
            lm_fin(0);
        }
        break;

    case LM_TX:
        r = lin_tx_step();
        if (r < 0) {
            lm_fin(0);
        } else if (r > 0) {
            if (!lm_toca_respuesta) {
                lm_tx_count++;
                lm_fin(1);
            } else {
                lin_empezar_rx(LIN_LEN + 1);  // el esclavo completa con datos y checksum
                lm_estado = LM_RX;
            }
        }
        break;

    default:  // LM_RX
        r = lin_rx_step();
        if (r < 0) {
            lm_fin(0);
        } else if (r > 0) {
            const uint8_t *d = lin.rx;
            int ok = d[LIN_LEN] == lin_checksum(lin_pid(LIN_ID_ESCLAVO), d, LIN_LEN) &&
                     d[0] == lm_cont && d[1] == (uint8_t)~d[0] && d[2] == 0xA5 && d[3] == 0x5A;
            lm_fin(ok);
        }
        break;
    }

    if (lm_error || (ticks - lm_t_ok) > LIN_RX_TIMEOUT_MS) leds(1, 0);
    else gpio_poner(LED_PORT, LED_ROJO, 0);
}

static void lm_deinit(void)
{
    lin_hw_deinit();
}

static void lm_estado_imprimir(void)
{
    cprintf("  tramas 0x10 enviadas %u, errores de eco %u\n", lm_tx_count, lin_eco_err);
}

static const prueba_t p_lin_maestro = {
    .nombre = "lin_maestro", .archivo = "LIN_Maestro",
    .descripcion = "LIN 19200 entre placas: manda 0x10 y pide 0x11 cada 50 ms (SW3 cerrado)",
    .init = lm_init, .step = lm_step, .deinit = lm_deinit, .estado = lm_estado_imprimir,
    .ok = &lm_resp_ok, .err = &lm_resp_err,
};
