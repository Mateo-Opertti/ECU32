// CAN_Maestro: CAN entre dos placas por FDCAN1 (la otra corre CAN_Esclavo)
// Cada 100 ms manda una trama ID 0x100 con 64 bytes: seq, seq+1, seq+2, ...
// El esclavo la verifica y contesta con la trama ID 0x101, con cada byte invertido
// LEDs: el verde (PF10) alterna con cada respuesta correcta del esclavo,
//       el rojo (PF9) se prende 1 s si llega una respuesta mal o falta,
//       y queda fijo si pasan 500 ms sin una buena
// 500 kbit/s nominal, 2 Mbit/s en datos (BRS), 64 bytes
// Hardware: unir CANH1-CANH1, CANL1-CANL1 y GND entre placas, con 120 ohm en cada punta del bus
//
// Consola:
// - cada 1 s (si "resumen on"): tramas por segundo, respuestas OK, errores, tiempo de ida y vuelta
//   (RTT) y estado del bus (TEC, REC, error activo / pasivo / bus off)
// - en el momento: el esclavo deja de contestar o vuelve, respuesta mal (con el byte que falla),
//   cambios de estado del bus y bus off
// - con "traza on": cada trama mandada y recibida

#define CM_ID_MAESTRO     0x100U
#define CM_ID_ESCLAVO     0x101U
#define CM_LEN            64U
#define CM_TX_MS          100U
#define CM_RX_TIMEOUT     500U
#define CM_ERROR_VISIBLE  1000U  // cuánto queda prendido el rojo después de un error
#define CM_RESUMEN_MS     1000U

static volatile uint32_t cm_tx;        // tramas 0x100 puestas en cola
static volatile uint32_t cm_ok;        // respuestas 0x101 correctas
static volatile uint32_t cm_err;       // respuestas que no llegaron o llegaron mal
static volatile uint32_t cm_sin_ack;   // veces que la trama anterior seguía sin que nadie la confirme
static volatile uint32_t cm_busoff;    // veces que el nodo se apagó por bus off y se reinició
static volatile uint32_t cm_psr;
static volatile uint32_t cm_lec, cm_dlec;  // último error: 1 stuff, 2 form, 3 ACK, 4 bit1, 5 bit0, 6 CRC

static uint8_t cm_seq, cm_esperado;
static int cm_esperando, cm_hay_error;
static uint32_t cm_t_tx, cm_t_ok, cm_t_err;

// Consola
static can_mon_t cm_mon;
static limite_t cm_lim;
static int cm_caido;                     // ya se avisó que el esclavo no contesta
static uint32_t cm_tx_caida;             // cm_tx cuando dejó de contestar
static uint32_t cm_t_envio_us;           // para el RTT
static uint32_t cm_rtt_suma, cm_rtt_n;
static uint32_t cm_t_res, cm_tx_res, cm_ok_res;

static void cm_init(void)
{
    can1_activar(FDCAN_MODO_BRS);
    cm_tx = cm_ok = cm_err = cm_sin_ack = cm_busoff = 0;
    cm_lec = cm_dlec = 0;
    cm_seq = cm_esperado = 0;
    cm_esperando = cm_hay_error = 0;
    cm_t_tx = cm_t_ok = cm_t_err = ticks;

    can_mon_init(&cm_mon, 0);
    cm_lim.usado = 0;
    cm_caido = 0;
    cm_rtt_suma = cm_rtt_n = 0;
    cm_t_res = ticks;
    cm_tx_res = cm_ok_res = 0;
}

// Explica por qué una trama recibida no es la respuesta esperada
static void cm_explicar(const can_trama_t *t)
{
    if (t->id != CM_ID_ESCLAVO) {
        cprintf("can_maestro: llego una trama con ID 0x%x, se esperaba 0x101\n", t->id & 0x1FFFFFFFU);
    } else if (!(t->r1 & ELEM_FDF) || !(t->r1 & ELEM_BRS) || t->len != CM_LEN) {
        cprintf("can_maestro: respuesta con formato mal (FD %u, BRS %u, %u bytes); se esperaba FD con BRS y 64 bytes\n",
                (t->r1 & ELEM_FDF) ? 1U : 0U, (t->r1 & ELEM_BRS) ? 1U : 0U, t->len);
    } else if (!cm_esperando) {
        cputs("can_maestro: llego una respuesta 0x101 que no se habia pedido (o llego tarde)\n");
    } else {
        for (uint32_t i = 0; i < CM_LEN; i++) {
            uint8_t esp = (uint8_t)~(uint8_t)(cm_esperado + i);
            if (t->data[i] != esp) {
                cprintf("can_maestro: respuesta mal: byte %u = 0x%02x, se esperaba 0x%02x\n", i, t->data[i], esp);
                break;
            }
        }
    }
}

static void cm_step(void)
{
    uint32_t p = fdcan_vigilar(FDCAN1_BASE, &cm_busoff);
    uint32_t lec = p & 7U, dlec = (p >> 8) & 7U;
    cm_psr = p;
    if (lec != 0 && lec != 7) cm_lec = lec;
    if (dlec != 0 && dlec != 7) cm_dlec = dlec;
    can_monitor("can_maestro", "CAN1", FDCAN1_BASE, p, cm_busoff, &cm_mon);

    if ((ticks - cm_t_tx) >= CM_TX_MS) {
        cm_t_tx = ticks;

        // Si nadie confirmó la anterior no se encola otra: al volver el esclavo
        // sale esa sola y no una tanda de tramas viejas
        if (FDCAN_TXBRP(FDCAN1_BASE) != 0) {
            cm_sin_ack++;
        } else {
            if (cm_esperando) {  // salió, pero el esclavo no contestó a tiempo
                cm_err++;
                cm_hay_error = 1;
                cm_t_err = ticks;
                if (!cm_caido && limite_ok(&cm_lim)) {
                    cputs("can_maestro: el esclavo no contesto la trama anterior en 100 ms\n");
                }
            }
            uint8_t buf[CM_LEN];
            for (uint32_t i = 0; i < CM_LEN; i++) buf[i] = (uint8_t)(cm_seq + i);
            if (fdcan_enviar(FDCAN1_BASE, CM_ID_MAESTRO, buf, CM_LEN, 1)) {
                cm_t_envio_us = micros();
                cm_esperado = cm_seq;
                cm_esperando = 1;
                cm_seq++;
                cm_tx++;
                if (traza_activa) {
                    can_traza("CAN1", "tx", CM_ID_MAESTRO, buf, CM_LEN);
                    cputs("\n");
                }
            }
        }
    }

    can_trama_t t;
    while (fdcan_recibir(FDCAN1_BASE, &t)) {
        int ok = t.id == CM_ID_ESCLAVO && (t.r1 & ELEM_FDF) && (t.r1 & ELEM_BRS) && t.len == CM_LEN;
        for (uint32_t i = 0; ok && i < CM_LEN; i++) {
            if (t.data[i] != (uint8_t)~(uint8_t)(cm_esperado + i)) ok = 0;  // cada byte invertido
        }
        uint32_t rtt = micros() - cm_t_envio_us;
        if (traza_activa) {
            can_traza("CAN1", "rx", t.id, t.data, t.len);
            if (ok && cm_esperando) cprintf("  RTT %u us\n", rtt);
            else cputs("  MAL\n");
        }
        if (ok && cm_esperando) {
            cm_ok++;
            cm_t_ok = ticks;
            cm_rtt_suma += rtt;
            cm_rtt_n++;
            led_alternar(LED_VERDE);
            if (cm_caido) {
                cprintf("can_maestro: el esclavo volvio (%u tramas sin respuesta)\n", cm_tx - cm_tx_caida);
                cm_caido = 0;
            }
        } else {
            cm_err++;
            cm_hay_error = 1;
            cm_t_err = ticks;
            if (limite_ok(&cm_lim)) cm_explicar(&t);
        }
        cm_esperando = 0;
    }

    if (!cm_caido && (ticks - cm_t_ok) > CM_RX_TIMEOUT) {
        cm_caido = 1;
        cm_tx_caida = cm_tx;
        if (FDCAN_TXBRP(FDCAN1_BASE) != 0) {
            cputs("can_maestro: sin respuesta hace 500 ms y nadie confirma las tramas: revisar cable CAN1, "
                  "terminacion (SW2) y que la otra placa corra can_esclavo\n");
        } else {
            cputs("can_maestro: sin respuesta hace 500 ms: las tramas salen pero el esclavo no contesta "
                  "(la otra placa no corre can_esclavo?)\n");
        }
    }

    if (cm_hay_error && (ticks - cm_t_err) > CM_ERROR_VISIBLE) cm_hay_error = 0;

    if (cm_hay_error || (ticks - cm_t_ok) > CM_RX_TIMEOUT) leds(1, 0);
    else gpio_poner(LED_PORT, LED_ROJO, 0);

    if ((ticks - cm_t_res) >= CM_RESUMEN_MS) {
        cm_t_res += CM_RESUMEN_MS;
        if (resumen_activo) {
            cprintf("can_maestro: tx %u/s  resp OK %u/s  err %u total", cm_tx - cm_tx_res, cm_ok - cm_ok_res, cm_err);
            if (cm_rtt_n) cprintf("  RTT %u us", cm_rtt_suma / cm_rtt_n);
            if (cm_sin_ack) cprintf("  sin ACK %u", cm_sin_ack);
            cputs(" | CAN1 ");
            can_imprimir_bus(FDCAN1_BASE, cm_psr);
            cputs("\n");
        }
        cm_tx_res = cm_tx;
        cm_ok_res = cm_ok;
        cm_rtt_suma = cm_rtt_n = 0;
    }
}

static void cm_deinit(void)
{
    can1_apagar();
}

static void cm_estado(void)
{
    cprintf("  enviadas %u, sin ACK %u, bus off %u, ultimo error %s (datos: %s) | ",
            cm_tx, cm_sin_ack, cm_busoff, can_lec_texto(cm_lec), can_lec_texto(cm_dlec));
    can_imprimir_bus(FDCAN1_BASE, cm_psr);
    cputs("\n");
}

static const prueba_t p_can_maestro = {
    .nombre = "can_maestro", .archivo = "CAN_Maestro",
    .descripcion = "CAN FD por CAN1 entre placas: manda 0x100 y espera 0x101",
    .init = cm_init, .step = cm_step, .deinit = cm_deinit, .estado = cm_estado,
    .ok = &cm_ok, .err = &cm_err,
};
