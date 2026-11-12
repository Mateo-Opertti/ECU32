// CAN_Esclavo: CAN entre dos placas por FDCAN1 (la otra corre CAN_Maestro)
// - ID 0x100: recibe los 64 bytes del maestro (seq, seq+1, ...) y los verifica
// - ID 0x101: si llegó bien, contesta con los mismos 64 bytes invertidos
// Juego de luces: no tiene temporizador propio, cada trama correcta avanza un paso.
// El valor de seq elige patrón y paso, y cada patrón dura 32 tramas (3,2 s):
//   0 alternado, 1 los dos juntos, 2 doble destello verde y rojo, 3 escalera
// Si llega una trama mal: rojo fijo 1 s. Si pasan 500 ms sin una buena: rojo fijo
// 500 kbit/s nominal, 2 Mbit/s en datos (BRS), 64 bytes
//
// Consola:
// - cada 1 s (si "resumen on"): tramas por segundo, errores, último seq, patrón y paso del juego,
//   saltos de secuencia, respuestas y estado del bus
// - en el momento: el maestro deja de mandar o vuelve, trama mal (con el byte que falla),
//   salto de secuencia, respuesta que no entró, cambios de estado del bus y bus off
// - con "traza on": cada trama recibida y cada respuesta

#define CE_ID_MAESTRO     0x100U
#define CE_ID_ESCLAVO     0x101U
#define CE_LEN            64U
#define CE_RX_TIMEOUT     500U
#define CE_ERROR_VISIBLE  1000U
#define CE_RESUMEN_MS     1000U

// Estado de los LEDs en cada paso del juego
#define CE_APAG  0U
#define CE_V     1U  // verde
#define CE_R     2U  // rojo
#define CE_VR    3U  // los dos

static const uint8_t ce_patrones[4][8] = {
    { CE_V,    CE_R,    CE_V,    CE_R,    CE_V,    CE_R,    CE_V,    CE_R    },  // alternado
    { CE_VR,   CE_APAG, CE_VR,   CE_APAG, CE_VR,   CE_APAG, CE_VR,   CE_APAG },  // los dos juntos
    { CE_V,    CE_APAG, CE_V,    CE_APAG, CE_R,    CE_APAG, CE_R,    CE_APAG },  // doble destello
    { CE_APAG, CE_V,    CE_R,    CE_VR,   CE_VR,   CE_R,    CE_V,    CE_APAG },  // escalera
};
static const char *const ce_patron_nombre[4] = { "alternado", "los dos juntos", "doble destello", "escalera" };

static volatile uint32_t ce_ok;        // tramas 0x100 recibidas bien
static volatile uint32_t ce_err;       // tramas que llegaron con algo mal
static volatile uint32_t ce_saltos;    // veces que seq no fue el siguiente
static volatile uint32_t ce_resp;      // respuestas 0x101 puestas en cola
static volatile uint32_t ce_llena;     // respuestas que no entraron porque el TX FIFO estaba lleno
static volatile uint32_t ce_busoff;
static volatile uint32_t ce_psr;
static volatile uint32_t ce_lec, ce_dlec;

static uint8_t ce_luces, ce_seq_anterior;
static int ce_primera, ce_hay_error;
static uint32_t ce_t_ok, ce_t_err;

// Consola
static can_mon_t ce_mon;
static limite_t ce_lim;
static int ce_caido;
static uint32_t ce_t_res, ce_ok_res, ce_resp_res;

static void ce_init(void)
{
    can1_activar(FDCAN_MODO_BRS);
    ce_ok = ce_err = ce_saltos = ce_resp = ce_llena = ce_busoff = 0;
    ce_lec = ce_dlec = 0;
    ce_luces = CE_APAG;
    ce_seq_anterior = 0;
    ce_primera = 1;
    ce_hay_error = 0;
    ce_t_ok = ce_t_err = ticks;

    can_mon_init(&ce_mon, 0);
    ce_lim.usado = 0;
    ce_caido = 0;
    ce_t_res = ticks;
    ce_ok_res = ce_resp_res = 0;
}

// Explica por qué una trama recibida no es la que manda el maestro
static void ce_explicar(const can_trama_t *t)
{
    if (t->id != CE_ID_MAESTRO) {
        cprintf("can_esclavo: llego una trama con ID 0x%x, se esperaba 0x100\n", t->id & 0x1FFFFFFFU);
    } else if (!(t->r1 & ELEM_FDF) || !(t->r1 & ELEM_BRS) || t->len != CE_LEN) {
        cprintf("can_esclavo: trama con formato mal (FD %u, BRS %u, %u bytes); se esperaba FD con BRS y 64 bytes\n",
                (t->r1 & ELEM_FDF) ? 1U : 0U, (t->r1 & ELEM_BRS) ? 1U : 0U, t->len);
    } else {
        for (uint32_t i = 1; i < CE_LEN; i++) {
            uint8_t esp = (uint8_t)(t->data[0] + i);
            if (t->data[i] != esp) {
                cprintf("can_esclavo: trama mal: byte %u = 0x%02x, se esperaba 0x%02x\n", i, t->data[i], esp);
                break;
            }
        }
    }
}

static void ce_step(void)
{
    uint32_t p = fdcan_vigilar(FDCAN1_BASE, &ce_busoff);
    uint32_t lec = p & 7U, dlec = (p >> 8) & 7U;
    ce_psr = p;
    if (lec != 0 && lec != 7) ce_lec = lec;
    if (dlec != 0 && dlec != 7) ce_dlec = dlec;
    can_monitor("can_esclavo", "CAN1", FDCAN1_BASE, p, ce_busoff, &ce_mon);

    can_trama_t t;
    while (fdcan_recibir(FDCAN1_BASE, &t)) {
        int ok = t.id == CE_ID_MAESTRO && (t.r1 & ELEM_FDF) && (t.r1 & ELEM_BRS) && t.len == CE_LEN;
        for (uint32_t i = 1; ok && i < CE_LEN; i++) {
            if (t.data[i] != (uint8_t)(t.data[0] + i)) ok = 0;
        }
        if (traza_activa) {
            can_traza("CAN1", "rx", t.id, t.data, t.len);
            cputs(ok ? "\n" : "  MAL\n");
        }
        if (ok) {
            uint8_t seq = t.data[0];
            ce_ok++;
            ce_t_ok = ticks;
            if (!ce_primera && seq != (uint8_t)(ce_seq_anterior + 1)) {
                ce_saltos++;
                if (!ce_caido && limite_ok(&ce_lim)) {
                    cprintf("can_esclavo: salto de secuencia: llego %u, se esperaba %u (se perdieron tramas o se reinicio el maestro)\n",
                            seq, (uint8_t)(ce_seq_anterior + 1));
                }
            }
            ce_primera = 0;
            ce_seq_anterior = seq;
            if (ce_caido) {
                cputs("can_esclavo: el maestro volvio\n");
                ce_caido = 0;
            }

            ce_luces = ce_patrones[(seq >> 5) & 3U][seq & 7U];

            uint8_t resp[CE_LEN];
            for (uint32_t i = 0; i < CE_LEN; i++) resp[i] = (uint8_t)~t.data[i];
            if (fdcan_enviar(FDCAN1_BASE, CE_ID_ESCLAVO, resp, CE_LEN, 1)) {
                ce_resp++;
                if (traza_activa) {
                    can_traza("CAN1", "tx", CE_ID_ESCLAVO, resp, CE_LEN);
                    cputs("\n");
                }
            } else {
                ce_llena++;
                if (limite_ok(&ce_lim)) cputs("can_esclavo: la respuesta no entro: TX FIFO lleno (el bus no confirma las tramas)\n");
            }
        } else {
            ce_err++;
            ce_hay_error = 1;
            ce_t_err = ticks;
            if (limite_ok(&ce_lim)) ce_explicar(&t);
        }
    }

    if (!ce_caido && (ticks - ce_t_ok) > CE_RX_TIMEOUT) {
        ce_caido = 1;
        cputs("can_esclavo: sin tramas del maestro hace 500 ms: revisar cable CAN1, terminacion (SW2) "
              "y que la otra placa corra can_maestro\n");
    }

    if (ce_hay_error && (ticks - ce_t_err) > CE_ERROR_VISIBLE) ce_hay_error = 0;

    uint8_t l = (ce_hay_error || (ticks - ce_t_ok) > CE_RX_TIMEOUT) ? CE_R : ce_luces;
    leds(l & CE_R, l & CE_V);

    if ((ticks - ce_t_res) >= CE_RESUMEN_MS) {
        ce_t_res += CE_RESUMEN_MS;
        if (resumen_activo) {
            uint8_t s = ce_seq_anterior;
            cprintf("can_esclavo: rx OK %u/s  err %u total  resp %u/s", ce_ok - ce_ok_res, ce_err, ce_resp - ce_resp_res);
            if (!ce_primera) cprintf("  seq %u  patron %s paso %u", s, ce_patron_nombre[(s >> 5) & 3U], (s & 7U) + 1);
            if (ce_saltos) cprintf("  saltos %u", ce_saltos);
            cputs(" | CAN1 ");
            can_imprimir_bus(FDCAN1_BASE, ce_psr);
            cputs("\n");
        }
        ce_ok_res = ce_ok;
        ce_resp_res = ce_resp;
    }
}

static void ce_deinit(void)
{
    can1_apagar();
}

static void ce_estado(void)
{
    cprintf("  respuestas %u, TX lleno %u, saltos de seq %u, bus off %u, ultimo error %s (datos: %s) | ",
            ce_resp, ce_llena, ce_saltos, ce_busoff, can_lec_texto(ce_lec), can_lec_texto(ce_dlec));
    can_imprimir_bus(FDCAN1_BASE, ce_psr);
    cputs("\n");
}

static const prueba_t p_can_esclavo = {
    .nombre = "can_esclavo", .archivo = "CAN_Esclavo",
    .descripcion = "CAN FD por CAN1 entre placas: recibe 0x100, contesta 0x101 y juego de luces",
    .init = ce_init, .step = ce_step, .deinit = ce_deinit, .estado = ce_estado,
    .ok = &ce_ok, .err = &ce_err,
};
