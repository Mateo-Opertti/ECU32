// Enlace de control entre placas por FDCAN2 (PB6 TX, PB5 RX, STB2 = PD15)
// CAN FD sin BRS: 500 kbit/s en toda la trama, igual que la fase nominal de las pruebas.
// Se usa FD y no CAN clásico para que la respuesta a "estado" (20 bytes) entre en una sola trama
//
// Comando, ID 0x700, maestro -> esclava, 8 bytes:
//   [0] comando (1 correr, 2 parar, 3 estado)  [1] número de prueba  [2] secuencia  [3..7] 0
// Respuesta, ID 0x701, esclava -> maestro, 20 bytes:
//   [0] ACK (0x06) o NACK (0x15)  [1] secuencia del comando  [2] rol  [3] prueba actual (0 = ninguna)
//   [4] código de error  [5] bit 0: hay contadores  [6..7] 0
//   [8..11] segundos corriendo  [12..15] tramas OK  [16..19] tramas con error (little endian)
//
// Texto, ID 0x702, esclava -> maestro, hasta 64 bytes:
//   [0] secuencia de texto  [1] bit 0: primera trama desde el arranque  [2] cantidad de caracteres (1..61)
//   [3..] caracteres
// Todo lo que la esclava imprime en su consola sale también por acá. El maestro arma las líneas y las
// imprime en su consola (USART1, PE0/PE1) con el prefijo "[esclava] "
//
// El maestro manda y reintenta 3 veces más, cada 100 ms, hasta recibir la respuesta con su secuencia.
// La esclava recuerda la última respuesta: si llega el mismo comando repetido (se perdió el ACK)
// la reenvía sin volver a ejecutarlo. Si una placa MAESTRO recibe un 0x700, hay dos maestros en el bus.

#define ENL_CMD_CORRER   1U
#define ENL_CMD_PARAR    2U
#define ENL_CMD_ESTADO   3U
#define ENL_ACK          0x06U
#define ENL_NACK         0x15U
#define ENL_LEN_CMD      8U
#define ENL_LEN_RESP     20U
#define ENL_TIMEOUT_MS   100U
#define ENL_REINTENTOS   3U
#define ENL_REPETIDO_MS  1000U  // ventana para reconocer un comando repetido
#define ENL_AVISO_MS     2000U  // como mucho un aviso de "dos maestros" cada 2 s
#define TXT_MAX          61U    // caracteres por trama de texto (64 - 3 de cabecera)
#define TXT_ESPERA_MS    20U    // un pedazo de línea sin '\n' sale igual a los 20 ms
#define TXT_LINEA_MAX    160U   // largo máximo de una línea armada en el maestro
#define TXT_CORTE_MS     100U   // el maestro imprime una línea incompleta si no llega el resto

// Códigos de error de la respuesta
#define ERR_OK           0U
#define ERR_COMANDO      1U  // comando desconocido
#define ERR_PRUEBA       2U  // número de prueba inválido
#define ERR_SOLO_LOCAL   3U  // la prueba solo se puede correr por la consola de esa placa
#define ERR_ES_MAESTRO   4U  // el que recibió el comando también es MAESTRO

// Acciones a hacer en el maestro cuando llega el ACK
#define ACCION_NINGUNA     0U
#define ACCION_PAR_WAKEUP  1U

static int enlace_activo;
volatile uint32_t enl_busoff;     // veces que FDCAN2 cayó en bus off
volatile uint32_t enl_sin_resp;   // pedidos del maestro que se quedaron sin respuesta

static struct {
    int pendiente;
    uint8_t cmd, prueba, seq;
    uint32_t intentos, t_envio, accion;
} pedido;
static uint8_t seq_sig;

static struct {
    int valida;
    uint8_t cmd, prueba, seq;
    uint32_t t;
    uint8_t resp[ENL_LEN_RESP];
} ultima;
static uint32_t t_aviso;
static int aviso_hecho;

// Texto de la esclava
static uint8_t txt_seq;              // esclava: secuencia de la próxima trama
static int txt_primera = 1;          // esclava: todavía no mandó ninguna desde el arranque
static char txt_linea[TXT_LINEA_MAX + 1];  // maestro: línea que se está armando
static uint32_t txt_len, txt_t;
static uint8_t txt_seq_esperada;
static int txt_sincronizado;
volatile uint32_t txt_perdidas;      // maestro: tramas de texto que no llegaron

static const char *enlace_error_texto(uint32_t e)
{
    switch (e) {
    case ERR_OK:         return "ok";
    case ERR_COMANDO:    return "comando desconocido";
    case ERR_PRUEBA:     return "numero de prueba invalido";
    case ERR_SOLO_LOCAL: return "esa prueba solo se corre desde la consola de su placa";
    case ERR_ES_MAESTRO: return "la otra placa tambien es MAESTRO";
    default:             return "error desconocido";
    }
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void enlace_init(void)
{
    gpio_salida(CAN_STB_PORT, CAN2_STB, 0);  // TCAN1046 de CAN2 en modo normal
    gpio_af(GPIOB_BASE, CAN2_RX, 9);
    gpio_af(GPIOB_BASE, CAN2_TX, 9);
    fdcan_iniciar(FDCAN2_BASE, FDCAN_MODO_ENLACE);
    enlace_activo = 1;
    seq_sig = (uint8_t)SYST_CVR;  // así un maestro recién reseteado no repite la secuencia anterior
    consola_reenviar = rol_actual == ROL_ESCLAVA;  // desde el mensaje de arranque en adelante
}

// CAN_FD necesita FDCAN2 para otra cosa
static void enlace_suspender(void)
{
    if (!enlace_activo) return;
    fdcan_detener(FDCAN2_BASE);
    enlace_activo = 0;
    if (pedido.pendiente) {
        pedido.pendiente = 0;
        cputs("esclava: pedido cancelado\n");
    }
    cputs("AVISO: enlace CAN2 suspendido mientras corre CAN_FD (la esclava no recibe comandos)\n");
    consola_reenviar = 0;
    fwd_out = fwd_in;  // lo que faltaba mandar ya no puede salir
    fwd_lineas = 0;
}

static void enlace_reanudar(void)
{
    if (enlace_activo) return;
    fdcan_iniciar(FDCAN2_BASE, FDCAN_MODO_ENLACE);
    enlace_activo = 1;
    ultima.valida = 0;
    consola_reenviar = rol_actual == ROL_ESCLAVA;
    cputs("Enlace CAN2 activo de nuevo\n");
}

// Esclava: manda lo pendiente de la consola en tramas de texto. Sale una línea completa apenas
// está, o un pedazo si se juntaron 61 caracteres o si pasaron 20 ms. Deja siempre un lugar libre
// en el TX FIFO para las respuestas 0x701
static void enlace_texto_mandar(void)
{
    uint32_t n = fwd_in - fwd_out;
    if (n == 0) return;
    if (n < TXT_MAX && fwd_lineas == 0 && (ticks - fwd_t) < TXT_ESPERA_MS) return;
    if ((FDCAN_TXFQS(FDCAN2_BASE) & 7U) < 2) return;  // TFFL: lugares libres en el TX FIFO

    uint8_t d[64];
    uint32_t k = n < TXT_MAX ? n : TXT_MAX;
    d[0] = txt_seq++;
    d[1] = txt_primera ? 1U : 0U;
    d[2] = (uint8_t)k;
    for (uint32_t i = 0; i < k; i++) {
        uint8_t c = fwd_buf[fwd_out % FWD_TAM];
        fwd_out++;
        if (c == '\n') fwd_lineas--;
        d[3 + i] = c;
    }
    fwd_t = ticks;  // lo que quedó espera su turno desde ahora
    fdcan_enviar(FDCAN2_BASE, ENLACE_ID_TEXTO, d, 3 + k, 0);
    txt_primera = 0;
}

// 1 cuando no queda texto por mandar (CAN_Esclavo_WakeUp lo espera antes de dormirse)
static int enlace_texto_vacio(void)
{
    return !consola_reenviar || (fwd_in == fwd_out && FDCAN_TXBRP(FDCAN2_BASE) == 0);
}

// Maestro: imprime la línea armada con el prefijo
static void enlace_texto_volcar(void)
{
    txt_linea[txt_len] = '\0';
    cputs("[esclava] ");
    cputs(txt_linea);
    cputs("\n");
    txt_len = 0;
}

static void enlace_texto_recibido(const can_trama_t *t)
{
    const uint8_t *d = t->data;
    if (rol_actual != ROL_MAESTRO || t->len < 3) return;

    if (d[1] & 1U) {  // la esclava acaba de arrancar
        if (txt_len) enlace_texto_volcar();
        txt_sincronizado = 0;
    }
    if (txt_sincronizado && d[0] != txt_seq_esperada) {
        uint32_t faltan = (uint8_t)(d[0] - txt_seq_esperada);
        txt_perdidas += faltan;
        if (txt_len) enlace_texto_volcar();
        cprintf("[esclava] (se perdieron %u tramas de texto)\n", faltan);
    }
    txt_sincronizado = 1;
    txt_seq_esperada = (uint8_t)(d[0] + 1);

    uint32_t n = d[2];
    if (n > t->len - 3) n = t->len - 3;
    for (uint32_t i = 0; i < n; i++) {
        char c = (char)d[3 + i];
        if (c == '\n') {
            enlace_texto_volcar();
        } else if ((uint8_t)c >= 0x20 && (uint8_t)c <= 0x7E) {  // sin \r ni controles
            txt_linea[txt_len++] = c;
            if (txt_len == TXT_LINEA_MAX) enlace_texto_volcar();
        }
    }
    txt_t = ticks;
}

static void enlace_mandar_pedido(void)
{
    uint8_t d[ENL_LEN_CMD] = { pedido.cmd, pedido.prueba, pedido.seq, 0, 0, 0, 0, 0 };
    FDCAN_TXBCR(FDCAN2_BASE) = FDCAN_TXBRP(FDCAN2_BASE);  // descarta un intento anterior que no salió
    fdcan_enviar(FDCAN2_BASE, ENLACE_ID_CMD, d, ENL_LEN_CMD, 0);
    pedido.intentos++;
    pedido.t_envio = ticks;
}

// Solo en el MAESTRO. Devuelve 1 si el pedido salió
static int enlace_pedir(uint32_t cmd, uint32_t prueba, uint32_t accion)
{
    if (rol_actual != ROL_MAESTRO) {
        cputs("Solo el MAESTRO manda comandos a la esclava (ver \"rol\")\n");
        return 0;
    }
    if (!enlace_activo) {
        cputs("El enlace CAN2 esta suspendido mientras corre CAN_FD\n");
        return 0;
    }
    if (pedido.pendiente) {
        cputs("Todavia se espera la respuesta del pedido anterior\n");
        return 0;
    }
    pedido.pendiente = 1;
    pedido.cmd = (uint8_t)cmd;
    pedido.prueba = (uint8_t)prueba;
    pedido.seq = seq_sig++;
    pedido.intentos = 0;
    pedido.accion = accion;
    enlace_mandar_pedido();
    return 1;
}

// Esclava: ejecuta el comando y arma la respuesta
static void enlace_atender(const can_trama_t *t)
{
    uint8_t cmd = t->data[0], n = t->data[1], seq = t->data[2];

    // Mismo comando repetido: el maestro no vio el ACK. Se reenvía la respuesta sin ejecutarlo de nuevo
    if (ultima.valida && ultima.seq == seq && ultima.cmd == cmd && ultima.prueba == n &&
        (ticks - ultima.t) < ENL_REPETIDO_MS) {
        fdcan_enviar(FDCAN2_BASE, ENLACE_ID_RESP, ultima.resp, ENL_LEN_RESP, 0);
        return;
    }

    uint32_t err = ERR_OK;
    if (rol_actual == ROL_MAESTRO) {
        err = ERR_ES_MAESTRO;
        if (!aviso_hecho || (ticks - t_aviso) > ENL_AVISO_MS) {
            cputs("AVISO: llego un comando 0x700 por CAN2 y esta placa es MAESTRO: hay dos maestros en el bus\n");
            t_aviso = ticks;
            aviso_hecho = 1;
        }
    } else if (cmd == ENL_CMD_CORRER) {
        cprintf("[CAN2] el maestro pide: correr %u\n", n);
        err = (uint32_t)prueba_correr(n, 1);
    } else if (cmd == ENL_CMD_PARAR) {
        cputs("[CAN2] el maestro pide: parar\n");
        prueba_parar();
    } else if (cmd != ENL_CMD_ESTADO) {
        err = ERR_COMANDO;
    }

    uint8_t *r = ultima.resp;
    uint32_t ok = 0, mal = 0;
    int hay = prueba_contadores(&ok, &mal);
    for (uint32_t i = 0; i < ENL_LEN_RESP; i++) r[i] = 0;
    r[0] = err == ERR_OK ? ENL_ACK : ENL_NACK;
    r[1] = seq;
    r[2] = (uint8_t)rol_actual;
    r[3] = (uint8_t)prueba_actual();
    r[4] = (uint8_t)err;
    r[5] = hay ? 1U : 0U;
    put32(&r[8], prueba_segundos());
    put32(&r[12], ok);
    put32(&r[16], mal);
    fdcan_enviar(FDCAN2_BASE, ENLACE_ID_RESP, r, ENL_LEN_RESP, 0);

    ultima.valida = 1;
    ultima.cmd = cmd;
    ultima.prueba = n;
    ultima.seq = seq;
    ultima.t = ticks;
}

// Maestro: muestra la respuesta del pedido en curso
static void enlace_respuesta(const can_trama_t *t)
{
    const uint8_t *r = t->data;
    if (rol_actual != ROL_MAESTRO) return;  // respuesta para otro: no es asunto de una esclava
    if (!pedido.pendiente || r[1] != pedido.seq) {
        cprintf("esclava: respuesta fuera de tiempo (secuencia %u), se ignora\n", r[1]);
        return;
    }
    pedido.pendiente = 0;

    if (r[0] != ENL_ACK) {
        cprintf("esclava: NACK (%s)\n", enlace_error_texto(r[4]));
        if (r[4] == ERR_ES_MAESTRO) cputs("AVISO: hay dos maestros en el bus. Cambia el rol de una con \"rol esclava\"\n");
        return;
    }
    cprintf("esclava: ACK  rol %s  prueba ", rol_nombre(r[2]));
    if (r[3] == 0) {
        cputs("ninguna\n");
    } else {
        cprintf("%u %s  corriendo %u s", r[3], prueba_nombre(r[3]), get32(&r[8]));
        if (r[5] & 1U) cprintf("  OK %u  error %u", get32(&r[12]), get32(&r[16]));
        cputs("\n");
    }
    if (pedido.accion == ACCION_PAR_WAKEUP) par_wakeup_ack();
}

static void enlace_step(void)
{
    if (!enlace_activo) return;
    fdcan_vigilar(FDCAN2_BASE, &enl_busoff);

    // El rol se puede cambiar en cualquier momento con "rol"
    consola_reenviar = rol_actual == ROL_ESCLAVA;
    if (consola_reenviar) {
        enlace_texto_mandar();
    } else {
        fwd_out = fwd_in;
        fwd_lineas = 0;
    }

    can_trama_t t;
    while (fdcan_recibir(FDCAN2_BASE, &t)) {
        if (t.id == ENLACE_ID_CMD && t.len >= ENL_LEN_CMD) enlace_atender(&t);
        else if (t.id == ENLACE_ID_RESP && t.len >= ENL_LEN_RESP) enlace_respuesta(&t);
        else if (t.id == ENLACE_ID_TEXTO) enlace_texto_recibido(&t);
    }

    // Una línea que quedó a medias (por ejemplo el eco de algo que se escribe en la esclava)
    if (txt_len && (ticks - txt_t) > TXT_CORTE_MS) enlace_texto_volcar();

    if (pedido.pendiente && (ticks - pedido.t_envio) >= ENL_TIMEOUT_MS) {
        if (pedido.intentos <= ENL_REINTENTOS) {
            enlace_mandar_pedido();
        } else {
            pedido.pendiente = 0;
            enl_sin_resp++;
            FDCAN_TXBCR(FDCAN2_BASE) = FDCAN_TXBRP(FDCAN2_BASE);
            cputs("esclava: sin respuesta\n");
        }
    }
}
