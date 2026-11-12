// Tráfico CAN con contador de secuencia y verificación en recepción
// - banco_can (COM-02, VR, INT-01): CAN1 <-> CAN2 de la misma placa, puenteados en J3, en los dos
//   sentidos a la vez. CAN clásico (8 bytes) o CAN FD (64 bytes con BRS), tasa y cantidad configurables
// - can_placas (EXT-02): las dos placas transmiten a la vez por CAN1 y por CAN2 con IDs distintos;
//   el arbitraje lo gana el ID menor y la perdedora se retransmite sola: no se tiene que perder nada
// - gateway / gw_fuente (EXT-03): la fuente manda CAN clásico por CAN1; el gateway lo reenvía por
//   CAN2 como CAN FD con BRS y la fuente mide pérdidas y latencia ida y vuelta
// Todas usan FDCAN2, así que suspenden el enlace de control mientras corren. Las de dos placas lo
// suspenden 100 ms después de arrancar (para que salga el ACK) y terminan solas: al llegar a la
// cantidad pedida o cuando la otra placa deja de transmitir. Al terminar vuelve el enlace y la
// esclava manda su informe al maestro
//
// Trama de prueba: bytes 0..3 = secuencia (little endian), byte i = (secuencia + i) desde el 4

#define BC_ID_12        0x110U   // banco_can: CAN1 -> CAN2
#define BC_ID_21        0x111U   // banco_can: CAN2 -> CAN1
#define CP_ID_MAESTRO   0x120U   // can_placas: lo que manda la placa MAESTRO (gana el arbitraje)
#define CP_ID_ESCLAVA   0x121U
#define GW_ID_ENTRADA   0x130U   // gateway: clásico por CAN1, de la fuente al gateway
#define GW_ID_SALIDA    0x131U   // gateway: FD con BRS por CAN2, del gateway a la fuente
#define BC_ESPERA_ENLACE_MS  100U   // las pruebas de dos placas apagan el enlace después de esto
#define BC_ESPERA_TX_MS      1000U  // y empiezan a transmitir después de esto, con la otra lista
#define BC_FIN_SIN_RX_MS     2000U  // fin: la otra placa dejó de transmitir

// Parámetros (comandos can_modo, can_tasa, can_cantidad; se pasan a la esclava por CAN2)
static uint32_t bc_fd = 1;              // 1 = CAN FD 64 bytes con BRS, 0 = CAN clásico 8 bytes
static uint32_t bc_tasa = 0;            // tramas por segundo por sentido (0 = lo más rápido posible)
static uint32_t bc_cantidad = 100000;   // tramas por sentido (0 = sin fin, hasta "parar")

typedef struct {
    const char *nombre;
    uint32_t tx_base, tx_id, rx_base, rx_id;
    uint32_t tx, rx_ok, perdidas, err, sig, t_prox, t_ult_rx;
} bc_flujo_t;

static bc_flujo_t bc_f[2];
static uint32_t bc_n;                    // flujos en uso
static const char *bc_prueba;            // nombre para los mensajes
static uint32_t bc_t_inicio, bc_espera_tx, bc_t_res, bc_t_ult_tx, bc_t_ult_rx;
static uint32_t bc_ok_res, bc_ajenas;
static volatile uint32_t bc_ok_total, bc_err_total;
static volatile uint32_t bc_busoff1, bc_busoff2;
static uint32_t bc_psr1, bc_psr2;
static can_mon_t bc_mon1, bc_mon2;
static limite_t bc_lim;
static int bc_fase;                      // 0 esperando para apagar el enlace, 1 corriendo, 2 terminó

static uint32_t bc_len(void) { return bc_fd ? 64U : 8U; }

static uint32_t bc_modo_fdcan(void) { return bc_fd ? FDCAN_MODO_BRS : 0U; }

static void bc_flujo(uint32_t i, const char *nombre, uint32_t txb, uint32_t txid, uint32_t rxb, uint32_t rxid)
{
    bc_flujo_t *f = &bc_f[i];
    f->nombre = nombre;
    f->tx_base = txb;
    f->tx_id = txid;
    f->rx_base = rxb;
    f->rx_id = rxid;
    f->tx = f->rx_ok = f->perdidas = f->err = f->sig = 0;
    f->t_prox = micros();
    f->t_ult_rx = ticks;
}

static void bc_motor_init(const char *prueba, uint32_t n_flujos, uint32_t espera_tx)
{
    bc_prueba = prueba;
    bc_n = n_flujos;
    bc_espera_tx = espera_tx;
    bc_t_inicio = bc_t_res = bc_t_ult_tx = ticks;
    bc_t_ult_rx = 0;
    bc_ok_res = bc_ajenas = 0;
    bc_ok_total = bc_err_total = 0;
    bc_busoff1 = bc_busoff2 = 0;
    can_mon_init(&bc_mon1, 0);
    can_mon_init(&bc_mon2, 0);
    bc_lim.usado = 0;
}

static void bc_enviar(bc_flujo_t *f)
{
    if (bc_cantidad && f->tx >= bc_cantidad) return;
    uint32_t ahora = micros();
    if (bc_tasa && (int32_t)(ahora - f->t_prox) < 0) return;

    uint8_t d[64];
    uint32_t len = bc_len();
    put32(d, f->tx);
    for (uint32_t i = 4; i < len; i++) d[i] = (uint8_t)(f->tx + i);
    if (!fdcan_enviar_ex(f->tx_base, f->tx_id, d, len, (int)bc_fd, (int)bc_fd)) return;  // FIFO lleno

    if (traza_activa) {
        can_traza(f->tx_base == FDCAN1_BASE ? "CAN1" : "CAN2", "tx", f->tx_id, d, len);
        cputs("\n");
    }
    f->tx++;
    bc_t_ult_tx = ticks;
    if (bc_tasa) {
        f->t_prox += 1000000U / bc_tasa;
        if ((int32_t)(ahora - f->t_prox) > 100000) f->t_prox = ahora;  // atrasado más de 100 ms: no recupera
    }
}

static void bc_recibir(uint32_t base)
{
    can_trama_t t;
    while (fdcan_recibir(base, &t)) {
        bc_flujo_t *f = 0;
        for (uint32_t i = 0; i < bc_n; i++) {
            if (bc_f[i].rx_base == base && bc_f[i].rx_id == t.id) f = &bc_f[i];
        }
        if (!f) {
            bc_ajenas++;  // por ejemplo, tramas del enlace de control de la otra placa
            continue;
        }
        uint32_t len = bc_len();
        uint32_t seq = get32(t.data);
        int ok = t.len == len && (bc_fd ? ((t.r1 & ELEM_FDF) && (t.r1 & ELEM_BRS)) : !(t.r1 & ELEM_FDF));
        uint32_t mal = 0;
        for (uint32_t i = 4; ok && i < len; i++) {
            if (t.data[i] != (uint8_t)(seq + i)) {
                ok = 0;
                mal = i;
            }
        }
        if (traza_activa) {
            can_traza(base == FDCAN1_BASE ? "CAN1" : "CAN2", "rx", t.id, t.data, t.len);
            cputs(ok ? "\n" : "  MAL\n");
        }
        f->t_ult_rx = bc_t_ult_rx = ticks;
        if (!ok) {
            f->err++;
            bc_err_total++;
            if (limite_ok(&bc_lim)) {
                if (mal) cprintf("%s: %s trama %u con el byte %u mal\n", bc_prueba, f->nombre, seq, mal);
                else cprintf("%s: %s trama con formato mal (FD %u, BRS %u, %u bytes)\n", bc_prueba, f->nombre,
                             (t.r1 & ELEM_FDF) ? 1U : 0U, (t.r1 & ELEM_BRS) ? 1U : 0U, t.len);
            }
            continue;
        }
        if (seq != f->sig) {
            if ((int32_t)(seq - f->sig) > 0) {
                f->perdidas += seq - f->sig;
                bc_err_total += seq - f->sig;
                if (limite_ok(&bc_lim)) {
                    cprintf("%s: %s se perdieron %u tramas (llego %u, se esperaba %u)\n",
                            bc_prueba, f->nombre, seq - f->sig, seq, f->sig);
                }
            } else {
                f->err++;
                bc_err_total++;
                if (limite_ok(&bc_lim)) cprintf("%s: %s trama repetida o fuera de orden (%u)\n", bc_prueba, f->nombre, seq);
            }
        }
        f->sig = seq + 1;
        f->rx_ok++;
        bc_ok_total++;
    }
}

static void bc_vigilar(void)
{
    bc_psr1 = fdcan_vigilar(FDCAN1_BASE, &bc_busoff1);
    can_monitor(bc_prueba, "CAN1", FDCAN1_BASE, bc_psr1, bc_busoff1, &bc_mon1);
    bc_psr2 = fdcan_vigilar(FDCAN2_BASE, &bc_busoff2);
    can_monitor(bc_prueba, "CAN2", FDCAN2_BASE, bc_psr2, bc_busoff2, &bc_mon2);
}

static void bc_imprimir(uint32_t tasa)
{
    cprintf("%s %s:", bc_prueba, bc_fd ? "FD" : "clasico");
    for (uint32_t i = 0; i < bc_n; i++) {
        bc_flujo_t *f = &bc_f[i];
        cprintf(" %s tx %u rx %u perd %u err %u |", f->nombre, f->tx, f->rx_ok, f->perdidas, f->err);
    }
    cprintf(" %u tramas/s | CAN1 ", tasa);
    can_imprimir_bus(FDCAN1_BASE, bc_psr1);
    cputs(" | CAN2 ");
    can_imprimir_bus(FDCAN2_BASE, bc_psr2);
    cputs("\n");
}

static void bc_resumen(void)
{
    uint32_t dt = ticks - bc_t_res;
    bc_t_res = ticks;
    uint32_t tasa = dt ? (bc_ok_total - bc_ok_res) * 1000U / dt : 0;
    bc_ok_res = bc_ok_total;
    if (resumen_activo) bc_imprimir(tasa);
}

// Un paso del tráfico: transmite (respetando la tasa y la espera inicial), recibe y vigila el bus
static void bc_motor_step(void)
{
    bc_vigilar();
    if ((ticks - bc_t_inicio) >= bc_espera_tx) {
        for (uint32_t i = 0; i < bc_n; i++) bc_enviar(&bc_f[i]);
    }
    bc_recibir(FDCAN1_BASE);
    bc_recibir(FDCAN2_BASE);
    if ((ticks - bc_t_res) >= 1000U) bc_resumen();
}

static int bc_tx_completo(void)
{
    if (!bc_cantidad) return 0;
    for (uint32_t i = 0; i < bc_n; i++) if (bc_f[i].tx < bc_cantidad) return 0;
    return 1;
}

// Informe final. "esperadas" = tramas que tendría que haber recibido cada flujo (0 = las que mandó)
static void bc_informe(uint32_t esperadas)
{
    uint32_t seg = (ticks - bc_t_inicio) / 1000U;
    uint32_t ecr1 = FDCAN_ECR(FDCAN1_BASE), ecr2 = FDCAN_ECR(FDCAN2_BASE);
    int pasa = 1;
    cprintf("==== %s %s, %u s ====\n", bc_prueba, bc_fd ? "CAN FD 500 kbit/s / 2 Mbit/s, 64 bytes" : "CAN clasico 500 kbit/s, 8 bytes", seg);
    for (uint32_t i = 0; i < bc_n; i++) {
        bc_flujo_t *f = &bc_f[i];
        uint32_t esp = esperadas ? esperadas : f->tx;
        cprintf("  %s: mandadas %u, recibidas bien %u de %u, perdidas %u, con error %u\n",
                f->nombre, f->tx, f->rx_ok, esp, f->perdidas, f->err);
        if (f->perdidas || f->err || f->rx_ok < esp || !f->rx_ok) pasa = 0;
    }
    cprintf("  CAN1: TEC %u REC %u, bus off %u | CAN2: TEC %u REC %u, bus off %u | tramas ajenas %u\n",
            ecr1 & 0xFFU, (ecr1 >> 8) & 0x7FU, bc_busoff1, ecr2 & 0xFFU, (ecr2 >> 8) & 0x7FU, bc_busoff2, bc_ajenas);
    if ((ecr1 & 0x7FFFU) || (ecr2 & 0x7FFFU) || bc_busoff1 || bc_busoff2) pasa = 0;
    cprintf("  Resultado: %s (criterio: 0 perdidas, 0 errores, TEC = REC = 0)\n", pasa ? "PASA" : "FALLA");
}

static void bc_leds(void)
{
    // Verde: llegan tramas en el último medio segundo. Rojo: hubo algún error
    leds(bc_err_total != 0, bc_t_ult_rx && (ticks - bc_t_ult_rx) < 500U);
}

// ---------------------------------------------------------------------------------------------
// banco_can: CAN1 <-> CAN2 en la misma placa

static void banco_init(void)
{
    enlace_suspender();
    can1_activar(bc_modo_fdcan());
    fdcan_iniciar(FDCAN2_BASE, bc_modo_fdcan());
    bc_motor_init("banco_can", 2, 0);
    bc_flujo(0, "CAN1->CAN2", FDCAN1_BASE, BC_ID_12, FDCAN2_BASE, BC_ID_12);
    bc_flujo(1, "CAN2->CAN1", FDCAN2_BASE, BC_ID_21, FDCAN1_BASE, BC_ID_21);
    bc_fase = 1;
    cprintf("banco_can: %s, %u tramas por sentido (%s), %s\n", bc_fd ? "CAN FD 64 bytes con BRS" : "CAN clasico 8 bytes",
            bc_cantidad, bc_cantidad ? "despues termina solo" : "sin fin", bc_tasa ? "con tasa fija" : "a la tasa maxima");
}

static void banco_step(void)
{
    bc_motor_step();
    bc_leds();
    // Terminó: se mandó todo y llegó todo, o pasó medio segundo desde la última trama
    if (bc_tx_completo()) {
        int todo = 1;
        for (uint32_t i = 0; i < bc_n; i++) if (bc_f[i].rx_ok + bc_f[i].perdidas + bc_f[i].err < bc_f[i].tx) todo = 0;
        if (todo || (ticks - bc_t_ult_tx) > 500U) prueba_parar();
    }
}

static void banco_deinit(void)
{
    can1_apagar();
    enlace_reanudar();
    bc_informe(0);
}

static void banco_estado(void)
{
    cprintf("  modo %s, tasa %u/s (0 = maxima), cantidad %u\n  ", bc_fd ? "FD" : "clasico", bc_tasa, bc_cantidad);
    if (bc_fase) bc_imprimir(0);
    else cputs("todavia no empezo\n");
}

static const prueba_t p_banco_can = {
    .nombre = "banco_can", .archivo = "Banco_CAN",
    .descripcion = "CAN1 <-> CAN2 en esta placa (puente en J3), clasico o FD, N tramas por sentido (COM-02)",
    .init = banco_init, .step = banco_step, .deinit = banco_deinit, .estado = banco_estado,
    .ok = &bc_ok_total, .err = &bc_err_total,
    .flags = PRUEBA_SOLO_LOCAL,
};

// ---------------------------------------------------------------------------------------------
// can_placas: las dos placas transmiten a la vez por CAN1 y CAN2

static uint32_t cp_t;

// Informe de la esclava: espera a que el maestro también haya vuelto al enlace
static void cp_informe_diferido(void) { bc_informe(bc_cantidad); }

static void cp_init(void)
{
    bc_fase = 0;
    cp_t = ticks;
    leds(0, 0);
}

static void cp_step(void)
{
    if (bc_fase == 0) {
        if ((ticks - cp_t) < BC_ESPERA_ENLACE_MS) return;
        enlace_suspender();
        can1_activar(bc_modo_fdcan());
        fdcan_iniciar(FDCAN2_BASE, bc_modo_fdcan());
        uint32_t mio = rol_actual == ROL_MAESTRO ? CP_ID_MAESTRO : CP_ID_ESCLAVA;
        uint32_t otro = rol_actual == ROL_MAESTRO ? CP_ID_ESCLAVA : CP_ID_MAESTRO;
        bc_motor_init("can_placas", 2, BC_ESPERA_TX_MS);
        bc_flujo(0, "CAN1", FDCAN1_BASE, mio, FDCAN1_BASE, otro);
        bc_flujo(1, "CAN2", FDCAN2_BASE, mio, FDCAN2_BASE, otro);
        bc_fase = 1;
        return;
    }
    bc_motor_step();
    bc_leds();

    // Fin: mandé todo y recibí todo, o la otra placa dejó de transmitir, o nunca llegó nada
    uint32_t desde = ticks - bc_t_inicio;
    int recibido = 1;
    for (uint32_t i = 0; i < bc_n; i++) if (!bc_cantidad || bc_f[i].rx_ok + bc_f[i].perdidas < bc_cantidad) recibido = 0;
    int otra_paro = bc_t_ult_rx && (ticks - bc_t_ult_rx) > BC_FIN_SIN_RX_MS;
    int nunca = !bc_t_ult_rx && desde > 10000U;
    if ((bc_tx_completo() && (recibido || otra_paro)) || (otra_paro && !bc_cantidad) || nunca) {
        bc_fase = 2;
        prueba_parar();
    }
}

static void cp_deinit(void)
{
    if (bc_fase == 0) return;  // no llegó a empezar
    can1_apagar();
    enlace_reanudar();
    if (rol_actual == ROL_ESCLAVA) diferir(cp_informe_diferido, 1500U);
    else bc_informe(bc_cantidad);
}

static const prueba_t p_can_placas = {
    .nombre = "can_placas", .archivo = "CAN_Placas",
    .descripcion = "Las dos placas transmiten a la vez por CAN1 y CAN2, con arbitraje (EXT-02)",
    .init = cp_init, .step = cp_step, .deinit = cp_deinit, .estado = banco_estado,
    .ok = &bc_ok_total, .err = &bc_err_total,
};

// ---------------------------------------------------------------------------------------------
// gateway: CAN1 (clásico) -> CAN2 (FD con BRS)

static volatile uint32_t gw_reenviadas, gw_descartadas;
static uint32_t gw_t_ult;

static void gw_init(void)
{
    bc_fase = 0;
    cp_t = ticks;
    gw_reenviadas = gw_descartadas = 0;
    gw_t_ult = 0;
    leds(0, 0);
}

static void gw_step(void)
{
    if (bc_fase == 0) {
        if ((ticks - cp_t) < BC_ESPERA_ENLACE_MS) return;
        enlace_suspender();
        can1_activar(FDCAN_MODO_BRS);              // recibe también CAN clásico
        fdcan_iniciar(FDCAN2_BASE, FDCAN_MODO_BRS);
        bc_motor_init("gateway", 0, 0);
        bc_fase = 1;
        cputs("gateway: reenvia ID 0x130 de CAN1 (clasico) a CAN2 como ID 0x131 en CAN FD con BRS\n");
        return;
    }
    bc_vigilar();
    can_trama_t t;
    while (fdcan_recibir(FDCAN1_BASE, &t)) {
        if (t.id != GW_ID_ENTRADA) {
            bc_ajenas++;
            continue;
        }
        gw_t_ult = ticks;
        if (fdcan_enviar_ex(FDCAN2_BASE, GW_ID_SALIDA, t.data, t.len, 1, 1)) gw_reenviadas++;
        else gw_descartadas++;  // CAN2 no da abasto
    }
    while (fdcan_recibir(FDCAN2_BASE, &t)) bc_ajenas++;
    leds(gw_descartadas != 0, gw_t_ult && (ticks - gw_t_ult) < 500U);

    if ((ticks - bc_t_res) >= 1000U) {
        bc_t_res = ticks;
        if (resumen_activo) cprintf("gateway: reenviadas %u, descartadas %u\n", gw_reenviadas, gw_descartadas);
    }
    // Fin: la fuente dejó de mandar, o nunca mandó nada en 30 s
    if ((gw_t_ult && (ticks - gw_t_ult) > BC_FIN_SIN_RX_MS) || (!gw_t_ult && (ticks - bc_t_inicio) > 30000U)) {
        bc_fase = 2;
        prueba_parar();
    }
}

static void gw_deinit(void)
{
    if (bc_fase == 0) return;
    can1_apagar();
    enlace_reanudar();
    cprintf("==== gateway ====\n  reenviadas %u, descartadas %u, ajenas %u | CAN1 ", gw_reenviadas, gw_descartadas, bc_ajenas);
    can_imprimir_bus(FDCAN1_BASE, bc_psr1);
    cputs(" | CAN2 ");
    can_imprimir_bus(FDCAN2_BASE, bc_psr2);
    cprintf("\n  Resultado: %s (las perdidas y la latencia las mide gw_fuente)\n", gw_descartadas ? "FALLA" : "PASA");
}

static const prueba_t p_gateway = {
    .nombre = "gateway", .archivo = "Gateway",
    .descripcion = "Reenvia CAN1 clasico (0x130) a CAN2 FD con BRS (0x131) (EXT-03, con gw_fuente)",
    .init = gw_init, .step = gw_step, .deinit = gw_deinit,
    .ok = &gw_reenviadas, .err = &gw_descartadas,
};

// ---------------------------------------------------------------------------------------------
// gw_fuente: manda por CAN1 y mide lo que vuelve por CAN2

static volatile uint32_t gf_tx, gf_rx, gf_perdidas, gf_err;
static uint32_t gf_sig, gf_lat_min, gf_lat_max, gf_t_prox;
static uint64_t gf_lat_suma;

static void gf_informe(void)
{
    uint32_t prom = gf_rx ? (uint32_t)(gf_lat_suma / gf_rx) : 0;
    int pasa = gf_rx && !gf_perdidas && !gf_err && gf_rx == gf_tx;
    cprintf("==== gw_fuente, %u s ====\n  mandadas %u (CAN1 clasico), vueltas %u (CAN2 FD), perdidas %u, con error %u\n",
            (ticks - bc_t_inicio) / 1000U, gf_tx, gf_rx, gf_perdidas, gf_err);
    cprintf("  latencia ida y vuelta: min %u us, prom %u us, max %u us\n", gf_rx ? gf_lat_min : 0, prom, gf_lat_max);
    cprintf("  Resultado: %s (criterio: 0 perdidas; la latencia se registra)\n", pasa ? "PASA" : "FALLA");
}

static void gf_init(void)
{
    bc_fase = 0;
    cp_t = ticks;
    gf_tx = gf_rx = gf_perdidas = gf_err = 0;
    gf_sig = 0;
    gf_lat_min = 0xFFFFFFFFU;
    gf_lat_max = 0;
    gf_lat_suma = 0;
    leds(0, 0);
}

static void gf_step(void)
{
    if (bc_fase == 0) {
        if ((ticks - cp_t) < BC_ESPERA_ENLACE_MS) return;
        enlace_suspender();
        can1_activar(FDCAN_MODO_BRS);
        fdcan_iniciar(FDCAN2_BASE, FDCAN_MODO_BRS);
        bc_motor_init("gw_fuente", 0, BC_ESPERA_TX_MS);
        gf_t_prox = micros();
        bc_fase = 1;
        return;
    }
    bc_vigilar();

    // Manda: secuencia y momento de envío en us
    if ((ticks - bc_t_inicio) >= BC_ESPERA_TX_MS && (!bc_cantidad || gf_tx < bc_cantidad)) {
        uint32_t ahora = micros();
        if (!bc_tasa || (int32_t)(ahora - gf_t_prox) >= 0) {
            uint8_t d[8];
            put32(d, gf_tx);
            put32(&d[4], ahora);
            if (fdcan_enviar_ex(FDCAN1_BASE, GW_ID_ENTRADA, d, 8, 0, 0)) {
                gf_tx++;
                bc_t_ult_tx = ticks;
                if (bc_tasa) gf_t_prox += 1000000U / bc_tasa;
            }
        }
    }

    can_trama_t t;
    while (fdcan_recibir(FDCAN2_BASE, &t)) {
        if (t.id != GW_ID_SALIDA) {
            bc_ajenas++;
            continue;
        }
        bc_t_ult_rx = ticks;
        if (!(t.r1 & ELEM_FDF) || !(t.r1 & ELEM_BRS) || t.len != 8) {
            gf_err++;
            if (limite_ok(&bc_lim)) cputs("gw_fuente: la trama no volvio como CAN FD con BRS de 8 bytes\n");
            continue;
        }
        uint32_t seq = get32(t.data);
        uint32_t lat = micros() - get32(&t.data[4]);
        if (seq != gf_sig) {
            if ((int32_t)(seq - gf_sig) > 0) gf_perdidas += seq - gf_sig;
            else gf_err++;
        }
        gf_sig = seq + 1;
        gf_rx++;
        gf_lat_suma += lat;
        if (lat < gf_lat_min) gf_lat_min = lat;
        if (lat > gf_lat_max) gf_lat_max = lat;
    }
    while (fdcan_recibir(FDCAN1_BASE, &t)) bc_ajenas++;
    leds(gf_perdidas || gf_err, bc_t_ult_rx && (ticks - bc_t_ult_rx) < 500U);

    if ((ticks - bc_t_res) >= 1000U) {
        bc_t_res = ticks;
        if (resumen_activo) {
            cprintf("gw_fuente: mandadas %u, vueltas %u, perdidas %u, err %u, latencia prom %u us\n",
                    gf_tx, gf_rx, gf_perdidas, gf_err, gf_rx ? (uint32_t)(gf_lat_suma / gf_rx) : 0);
        }
    }

    int tx_fin = bc_cantidad && gf_tx >= bc_cantidad;
    int sin_rx = bc_t_ult_rx && (ticks - bc_t_ult_rx) > BC_FIN_SIN_RX_MS;
    if ((tx_fin && (gf_rx + gf_perdidas >= gf_tx || (ticks - bc_t_ult_tx) > 1000U)) || sin_rx ||
        (!bc_t_ult_rx && (ticks - bc_t_inicio) > 10000U)) {
        bc_fase = 2;
        prueba_parar();
    }
}

static void gf_deinit(void)
{
    if (bc_fase == 0) return;
    can1_apagar();
    enlace_reanudar();
    if (rol_actual == ROL_ESCLAVA) diferir(gf_informe, 1500U);
    else gf_informe();
}

static const prueba_t p_gw_fuente = {
    .nombre = "gw_fuente", .archivo = "GW_Fuente",
    .descripcion = "Fuente del gateway: manda por CAN1 y mide perdidas y latencia en CAN2 (EXT-03)",
    .init = gf_init, .step = gf_step, .deinit = gf_deinit,
    .ok = &gf_rx, .err = &gf_perdidas,
};
