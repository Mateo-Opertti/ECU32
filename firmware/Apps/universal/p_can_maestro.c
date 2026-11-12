// CAN_Maestro: CAN entre dos placas por FDCAN1 (la otra corre CAN_Esclavo)
// Cada 100 ms manda una trama ID 0x100 con 64 bytes: seq, seq+1, seq+2, ...
// El esclavo la verifica y contesta con la trama ID 0x101, con cada byte invertido
// LEDs: el verde (PF10) alterna con cada respuesta correcta del esclavo,
//       el rojo (PF9) se prende 1 s si llega una respuesta mal o falta,
//       y queda fijo si pasan 500 ms sin una buena
// 500 kbit/s nominal, 2 Mbit/s en datos (BRS), 64 bytes
// Hardware: unir CANH1-CANH1, CANL1-CANL1 y GND entre placas, con 120 ohm en cada punta del bus

#define CM_ID_MAESTRO     0x100U
#define CM_ID_ESCLAVO     0x101U
#define CM_LEN            64U
#define CM_TX_MS          100U
#define CM_RX_TIMEOUT     500U
#define CM_ERROR_VISIBLE  1000U  // cuánto queda prendido el rojo después de un error

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

static void cm_init(void)
{
    can1_activar(FDCAN_MODO_BRS);
    cm_tx = cm_ok = cm_err = cm_sin_ack = cm_busoff = 0;
    cm_lec = cm_dlec = 0;
    cm_seq = cm_esperado = 0;
    cm_esperando = cm_hay_error = 0;
    cm_t_tx = cm_t_ok = cm_t_err = ticks;
}

static void cm_step(void)
{
    uint32_t p = fdcan_vigilar(FDCAN1_BASE, &cm_busoff);
    uint32_t lec = p & 7U, dlec = (p >> 8) & 7U;
    cm_psr = p;
    if (lec != 0 && lec != 7) cm_lec = lec;
    if (dlec != 0 && dlec != 7) cm_dlec = dlec;

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
            }
            uint8_t buf[CM_LEN];
            for (uint32_t i = 0; i < CM_LEN; i++) buf[i] = (uint8_t)(cm_seq + i);
            if (fdcan_enviar(FDCAN1_BASE, CM_ID_MAESTRO, buf, CM_LEN, 1)) {
                cm_esperado = cm_seq;
                cm_esperando = 1;
                cm_seq++;
                cm_tx++;
            }
        }
    }

    can_trama_t t;
    while (fdcan_recibir(FDCAN1_BASE, &t)) {
        int ok = t.id == CM_ID_ESCLAVO && (t.r1 & ELEM_FDF) && (t.r1 & ELEM_BRS) && t.len == CM_LEN;
        for (uint32_t i = 0; ok && i < CM_LEN; i++) {
            if (t.data[i] != (uint8_t)~(uint8_t)(cm_esperado + i)) ok = 0;  // cada byte invertido
        }
        if (ok && cm_esperando) {
            cm_ok++;
            cm_t_ok = ticks;
            led_alternar(LED_VERDE);
        } else {
            cm_err++;
            cm_hay_error = 1;
            cm_t_err = ticks;
        }
        cm_esperando = 0;
    }

    if (cm_hay_error && (ticks - cm_t_err) > CM_ERROR_VISIBLE) cm_hay_error = 0;

    if (cm_hay_error || (ticks - cm_t_ok) > CM_RX_TIMEOUT) leds(1, 0);
    else gpio_poner(LED_PORT, LED_ROJO, 0);
}

static void cm_deinit(void)
{
    can1_apagar();
}

static void cm_estado(void)
{
    cprintf("  enviadas %u, sin ACK %u, bus off %u, PSR 0x%x, ultimo LEC %u, DLEC %u\n",
            cm_tx, cm_sin_ack, cm_busoff, cm_psr, cm_lec, cm_dlec);
}

static const prueba_t p_can_maestro = {
    .nombre = "can_maestro", .archivo = "CAN_Maestro",
    .descripcion = "CAN FD por CAN1 entre placas: manda 0x100 y espera 0x101",
    .init = cm_init, .step = cm_step, .deinit = cm_deinit, .estado = cm_estado,
    .ok = &cm_ok, .err = &cm_err,
};
