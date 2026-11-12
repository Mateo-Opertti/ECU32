// CAN_Esclavo: CAN entre dos placas por FDCAN1 (la otra corre CAN_Maestro)
// - ID 0x100: recibe los 64 bytes del maestro (seq, seq+1, ...) y los verifica
// - ID 0x101: si llegó bien, contesta con los mismos 64 bytes invertidos
// Juego de luces: no tiene temporizador propio, cada trama correcta avanza un paso.
// El valor de seq elige patrón y paso, y cada patrón dura 32 tramas (3,2 s):
//   0 alternado, 1 los dos juntos, 2 doble destello verde y rojo, 3 escalera
// Si llega una trama mal: rojo fijo 1 s. Si pasan 500 ms sin una buena: rojo fijo
// 500 kbit/s nominal, 2 Mbit/s en datos (BRS), 64 bytes

#define CE_ID_MAESTRO     0x100U
#define CE_ID_ESCLAVO     0x101U
#define CE_LEN            64U
#define CE_RX_TIMEOUT     500U
#define CE_ERROR_VISIBLE  1000U

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
}

static void ce_step(void)
{
    uint32_t p = fdcan_vigilar(FDCAN1_BASE, &ce_busoff);
    uint32_t lec = p & 7U, dlec = (p >> 8) & 7U;
    ce_psr = p;
    if (lec != 0 && lec != 7) ce_lec = lec;
    if (dlec != 0 && dlec != 7) ce_dlec = dlec;

    can_trama_t t;
    while (fdcan_recibir(FDCAN1_BASE, &t)) {
        int ok = t.id == CE_ID_MAESTRO && (t.r1 & ELEM_FDF) && (t.r1 & ELEM_BRS) && t.len == CE_LEN;
        for (uint32_t i = 1; ok && i < CE_LEN; i++) {
            if (t.data[i] != (uint8_t)(t.data[0] + i)) ok = 0;
        }
        if (ok) {
            uint8_t seq = t.data[0];
            ce_ok++;
            ce_t_ok = ticks;
            if (!ce_primera && seq != (uint8_t)(ce_seq_anterior + 1)) ce_saltos++;
            ce_primera = 0;
            ce_seq_anterior = seq;

            ce_luces = ce_patrones[(seq >> 5) & 3U][seq & 7U];

            uint8_t resp[CE_LEN];
            for (uint32_t i = 0; i < CE_LEN; i++) resp[i] = (uint8_t)~t.data[i];
            if (fdcan_enviar(FDCAN1_BASE, CE_ID_ESCLAVO, resp, CE_LEN, 1)) ce_resp++;
            else ce_llena++;
        } else {
            ce_err++;
            ce_hay_error = 1;
            ce_t_err = ticks;
        }
    }

    if (ce_hay_error && (ticks - ce_t_err) > CE_ERROR_VISIBLE) ce_hay_error = 0;

    uint8_t l = (ce_hay_error || (ticks - ce_t_ok) > CE_RX_TIMEOUT) ? CE_R : ce_luces;
    leds(l & CE_R, l & CE_V);
}

static void ce_deinit(void)
{
    can1_apagar();
}

static void ce_estado(void)
{
    cprintf("  respuestas %u, TX lleno %u, saltos de seq %u, bus off %u, PSR 0x%x, LEC %u, DLEC %u\n",
            ce_resp, ce_llena, ce_saltos, ce_busoff, ce_psr, ce_lec, ce_dlec);
}

static const prueba_t p_can_esclavo = {
    .nombre = "can_esclavo", .archivo = "CAN_Esclavo",
    .descripcion = "CAN FD por CAN1 entre placas: recibe 0x100, contesta 0x101 y juego de luces",
    .init = ce_init, .step = ce_step, .deinit = ce_deinit, .estado = ce_estado,
    .ok = &ce_ok, .err = &ce_err,
};
