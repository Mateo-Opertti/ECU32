// Partes comunes de LIN_Maestro y LIN_Esclavo
// LIN 2.x a 19200 baud, checksum mejorado. USART3 en modo LIN: TX = PD8, RX = PD9, EN del TLIN4029 = PD10
// En vez de esperar cada byte en un while, el envío y la recepción avanzan de a un byte por step():
// un byte dura 520 us y el loop principal da muchas vueltas en ese tiempo

#define LIN_BAUD        19200U
#define LIN_SYNC        0x55U
#define LIN_ID_MAESTRO  0x10U  // trama que publica el maestro
#define LIN_ID_ESCLAVO  0x11U  // trama que publica el esclavo
#define LIN_LEN         4U
#define LIN_BYTE_TIMEOUT_MS 5U
#define LIN_RX_TIMEOUT_MS   500U

static volatile uint32_t lin_eco_err;  // bytes propios que no se leyeron bien en el bus

// Bytes a mandar (con verificación del eco) y bytes a recibir
static struct {
    uint8_t tx[8];
    uint32_t ntx, itx;
    int eco_pendiente;
    uint8_t rx[8];
    uint32_t nrx, irx;
    uint32_t t;  // último movimiento, para los timeouts
    int eco_leido;  // cuando falla un eco: el byte que volvió, o -1 si no volvió nada
} lin;

static void lin_hw_init(void)
{
    RCC_APB1ENR1 |= RCC_USART3EN;
    (void)RCC_APB1ENR1;

    gpio_af(LIN_PORT, LIN_TX, 7);
    gpio_af(LIN_PORT, LIN_RX, 7);
    gpio_salida(LIN_PORT, LIN_EN, 1);  // transceiver habilitado

    USART_CR1(USART3_BASE) = 0;
    USART_BRR(USART3_BASE) = (PCLK1_HZ + LIN_BAUD / 2) / LIN_BAUD;
    USART_CR2(USART3_BASE) = CR2_LINEN | CR2_LBDL;  // 8N1, modo LIN
    USART_CR1(USART3_BASE) = CR1_TE | CR1_RE | CR1_UE;
    lin_eco_err = 0;
}

static void lin_hw_deinit(void)
{
    USART_CR1(USART3_BASE) = 0;
    USART_CR2(USART3_BASE) = 0;
    RCC_APB1ENR1 &= ~RCC_USART3EN;
    gpio_poner(LIN_PORT, LIN_EN, 0);  // TLIN4029 en sleep
    gpio_analogico(LIN_PORT, LIN_TX);
    gpio_analogico(LIN_PORT, LIN_RX);
}

// Descarta lo que haya en el receptor y limpia los errores
static void lin_flush(void)
{
    while (USART_ISR(USART3_BASE) & ISR_RXNE) (void)USART_RDR(USART3_BASE);
    USART_ICR(USART3_BASE) = ICR_ERRORES;
}

// Identificador protegido: ID de 6 bits + 2 bits de paridad
static uint8_t lin_pid(uint8_t id)
{
    uint8_t b0 = id & 1, b1 = (id >> 1) & 1, b2 = (id >> 2) & 1;
    uint8_t b3 = (id >> 3) & 1, b4 = (id >> 4) & 1, b5 = (id >> 5) & 1;
    uint8_t p0 = b0 ^ b1 ^ b2 ^ b4;
    uint8_t p1 = !(b1 ^ b3 ^ b4 ^ b5);
    return (uint8_t)(id | (p0 << 6) | (p1 << 7));
}

// Checksum mejorado (LIN 2.x): suma con acarreo del PID y los datos, invertida
static uint8_t lin_checksum(uint8_t pid, const uint8_t *data, uint32_t len)
{
    uint32_t suma = pid;
    for (uint32_t i = 0; i < len; i++) {
        suma += data[i];
        if (suma > 0xFF) suma -= 0xFF;
    }
    return (uint8_t)~suma;
}

static void lin_empezar_tx(void)
{
    lin.itx = 0;
    lin.eco_pendiente = 0;
    lin.t = ticks;
}

static void lin_empezar_rx(uint32_t n)
{
    lin.nrx = n;
    lin.irx = 0;
    lin.t = ticks;
}

// LIN es un solo cable: todo lo que se manda vuelve por RX y se compara
// Devuelve 0 mientras sigue, 1 si salió todo bien, -1 si un eco no llegó o no coincide
static int lin_tx_step(void)
{
    if (lin.eco_pendiente) {
        if (USART_ISR(USART3_BASE) & ISR_RXNE) {
            uint8_t c = (uint8_t)USART_RDR(USART3_BASE);
            lin.eco_pendiente = 0;
            if (c != lin.tx[lin.itx]) {
                lin_eco_err++;
                lin.eco_leido = c;
                return -1;
            }
            if (++lin.itx == lin.ntx) return 1;
        } else if ((ticks - lin.t) > LIN_BYTE_TIMEOUT_MS) {
            lin_eco_err++;
            lin.eco_leido = -1;
            return -1;
        }
    } else if (USART_ISR(USART3_BASE) & ISR_TXE) {
        USART_TDR(USART3_BASE) = lin.tx[lin.itx];
        lin.eco_pendiente = 1;
        lin.t = ticks;
    }
    return 0;
}

// Explica por la consola por qué falló lin_tx_step
static void lin_explicar_eco(const char *prueba)
{
    if (lin.eco_leido < 0) {
        cprintf("%s: no volvio el eco del byte %u (0x%02x): el bus no se mueve (EN = PD10, transceptor o bus a GND)\n",
                prueba, lin.itx, lin.tx[lin.itx]);
    } else {
        cprintf("%s: eco distinto en el byte %u: se mando 0x%02x y se leyo 0x%02x (choque con otro nodo o ruido)\n",
                prueba, lin.itx, lin.tx[lin.itx], (uint32_t)lin.eco_leido);
    }
}

// "LIN 0x10 tx: 25 26 27 28  chk 1E" (d = datos + checksum, sin fin de línea)
static void lin_traza(uint8_t id, const char *dir, const uint8_t *d, uint32_t n_datos)
{
    cprintf("LIN 0x%02x %s:", id, dir);
    cput_bytes(d, n_datos);
    cprintf("  chk %02x", d[n_datos]);
}

// Devuelve 0 mientras sigue, 1 si llegaron los nrx bytes, -1 si pasaron 5 ms sin el siguiente
static int lin_rx_step(void)
{
    if (USART_ISR(USART3_BASE) & ISR_RXNE) {
        lin.rx[lin.irx++] = (uint8_t)USART_RDR(USART3_BASE);
        lin.t = ticks;
        if (lin.irx == lin.nrx) return 1;
    } else if ((ticks - lin.t) > LIN_BYTE_TIMEOUT_MS) {
        return -1;
    }
    return 0;
}
