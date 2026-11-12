// CAN_FD: FDCAN1 manda una trama CAN FD cada 100 ms y FDCAN2 la recibe, en la misma placa
// - Cada trama recibida bien alterna el LED verde (PF10)
// - El LED rojo (PF9) se prende si llega una trama mal o si pasan 500 ms sin recibir
// 500 kbit/s nominal, 2 Mbit/s en datos (BRS), 64 bytes, ID 0x123
// Hardware: unir CANH1-CANH2 y CANL1-CANL2 en J3, con terminación de 120 ohm
//
// Usa FDCAN2, que es el del enlace de control: solo se corre desde la consola de esta placa
// (nunca por CAN2) y suspende el enlace mientras corre. Al pararla, FDCAN2 vuelve al enlace.
// El cable de CAN2 hacia la otra placa hay que sacarlo: si no, el puente de J3 une los dos buses
//
// Consola:
// - cada 1 s (si "resumen on"): tramas mandadas y recibidas por segundo, errores y estado de los dos nodos
// - en el momento: FDCAN2 deja de recibir o vuelve, trama mal (con el byte que falla) y cambios de estado
// - con "traza on": cada trama mandada y recibida

#define CFD_TX_MS       100U
#define CFD_RX_TIMEOUT  500U
#define CFD_ID          0x123U
#define CFD_LEN         64U
#define CFD_RESUMEN_MS  1000U

static volatile uint32_t cfd_tx, cfd_ok, cfd_err;
static volatile uint32_t cfd_psr1, cfd_psr2;  // estado de protocolo (LEC, error passive, bus off)
static uint8_t cfd_seq;
static uint32_t cfd_t_tx, cfd_t_ok;
static int cfd_error;

// Consola
static can_mon_t cfd_mon1, cfd_mon2;
static limite_t cfd_lim;
static int cfd_caido;
static uint32_t cfd_t_res, cfd_tx_res, cfd_ok_res;

static void cfd_init(void)
{
    enlace_suspender();
    can1_activar(FDCAN_MODO_BRS);
    fdcan_iniciar(FDCAN2_BASE, FDCAN_MODO_BRS);  // STB2 y los pines de CAN2 ya están del enlace

    cfd_tx = cfd_ok = cfd_err = 0;
    cfd_seq = 0;
    cfd_error = 0;
    cfd_t_tx = cfd_t_ok = ticks;

    can_mon_init(&cfd_mon1, 0);
    can_mon_init(&cfd_mon2, 0);
    cfd_lim.usado = 0;
    cfd_caido = 0;
    cfd_t_res = ticks;
    cfd_tx_res = cfd_ok_res = 0;
}

static void cfd_explicar(const can_trama_t *t)
{
    if (t->id != CFD_ID) {
        cprintf("can_fd: FDCAN2 recibio una trama con ID 0x%x, se esperaba 0x123 (hay otro nodo en el bus?)\n",
                t->id & 0x1FFFFFFFU);
    } else if (!(t->r1 & ELEM_FDF) || !(t->r1 & ELEM_BRS) || t->len != CFD_LEN) {
        cprintf("can_fd: trama con formato mal (FD %u, BRS %u, %u bytes)\n",
                (t->r1 & ELEM_FDF) ? 1U : 0U, (t->r1 & ELEM_BRS) ? 1U : 0U, t->len);
    } else {
        for (uint32_t i = 1; i < CFD_LEN; i++) {
            uint8_t esp = (uint8_t)(t->data[0] + i);
            if (t->data[i] != esp) {
                cprintf("can_fd: trama mal: byte %u = 0x%02x, se esperaba 0x%02x\n", i, t->data[i], esp);
                break;
            }
        }
    }
}

static void cfd_step(void)
{
    // FDCAN1: manda 64 bytes que arrancan en seq y van subiendo de a 1
    if ((ticks - cfd_t_tx) >= CFD_TX_MS) {
        cfd_t_tx = ticks;
        uint8_t buf[CFD_LEN];
        for (uint32_t i = 0; i < CFD_LEN; i++) buf[i] = (uint8_t)(cfd_seq + i);
        if (fdcan_enviar(FDCAN1_BASE, CFD_ID, buf, CFD_LEN, 1)) {
            cfd_seq++;
            cfd_tx++;
            if (traza_activa) {
                can_traza("CAN1", "tx", CFD_ID, buf, CFD_LEN);
                cputs("\n");
            }
        }
    }

    // FDCAN2: verifica lo que llega
    can_trama_t t;
    while (fdcan_recibir(FDCAN2_BASE, &t)) {
        int ok = t.id == CFD_ID && (t.r1 & ELEM_FDF) && (t.r1 & ELEM_BRS) && t.len == CFD_LEN;
        for (uint32_t i = 1; ok && i < CFD_LEN; i++) {
            if (t.data[i] != (uint8_t)(t.data[0] + i)) ok = 0;
        }
        if (traza_activa) {
            can_traza("CAN2", "rx", t.id, t.data, t.len);
            cputs(ok ? "\n" : "  MAL\n");
        }
        if (ok) {
            cfd_ok++;
            cfd_t_ok = ticks;
            cfd_error = 0;
            led_alternar(LED_VERDE);
            if (cfd_caido) {
                cputs("can_fd: FDCAN2 volvio a recibir\n");
                cfd_caido = 0;
            }
        } else {
            cfd_err++;
            cfd_error = 1;
            if (limite_ok(&cfd_lim)) cfd_explicar(&t);
        }
    }

    if (!cfd_caido && (ticks - cfd_t_ok) > CFD_RX_TIMEOUT) {
        cfd_caido = 1;
        cputs("can_fd: FDCAN2 no recibe hace 500 ms: revisar el puente CANH1-CANH2 / CANL1-CANL2 en J3 "
              "y la terminacion (SW2 o SW4)\n");
    }

    if (cfd_error || (ticks - cfd_t_ok) > CFD_RX_TIMEOUT) leds(1, 0);
    else gpio_poner(LED_PORT, LED_ROJO, 0);

    cfd_psr1 = FDCAN_PSR(FDCAN1_BASE);
    cfd_psr2 = FDCAN_PSR(FDCAN2_BASE);
    can_monitor("can_fd", "CAN1", FDCAN1_BASE, cfd_psr1, 0, &cfd_mon1);
    can_monitor("can_fd", "CAN2", FDCAN2_BASE, cfd_psr2, 0, &cfd_mon2);

    if ((ticks - cfd_t_res) >= CFD_RESUMEN_MS) {
        cfd_t_res += CFD_RESUMEN_MS;
        if (resumen_activo) {
            cprintf("can_fd: tx %u/s  rx OK %u/s  err %u total | CAN1 ", cfd_tx - cfd_tx_res, cfd_ok - cfd_ok_res, cfd_err);
            can_imprimir_bus(FDCAN1_BASE, cfd_psr1);
            cputs(" | CAN2 ");
            can_imprimir_bus(FDCAN2_BASE, cfd_psr2);
            cputs("\n");
        }
        cfd_tx_res = cfd_tx;
        cfd_ok_res = cfd_ok;
    }
}

static void cfd_deinit(void)
{
    can1_apagar();
    enlace_reanudar();  // FDCAN2 vuelve a la configuración del enlace
}

static void cfd_estado(void)
{
    cprintf("  tramas enviadas %u | CAN1 ", cfd_tx);
    can_imprimir_bus(FDCAN1_BASE, cfd_psr1);
    cputs(" | CAN2 ");
    can_imprimir_bus(FDCAN2_BASE, cfd_psr2);
    cputs("\n");
}

static const prueba_t p_can_fd = {
    .nombre = "can_fd", .archivo = "CAN_FD",
    .descripcion = "FDCAN1 -> FDCAN2 en la misma placa (puente CAN1-CAN2 en J3)",
    .init = cfd_init, .step = cfd_step, .deinit = cfd_deinit, .estado = cfd_estado,
    .ok = &cfd_ok, .err = &cfd_err,
    .flags = PRUEBA_SOLO_LOCAL,
};
