// Driver común de FDCAN1 y FDCAN2
// Las dos instancias comparten reloj de kernel (RCC_CCIPR.FDCANSEL), divisor (FDCAN_CONFIG.CKDIV),
// enable en RCC y message RAM. Por eso:
// - el reloj se fija una sola vez al arrancar y ninguna prueba lo vuelve a tocar
// - nunca se resetea el FDCAN por RCC y CKDIV queda en su valor de reset (/1)
// - cada instancia borra y usa solo su parte de la message RAM
// Bit timing con kernel de 16 MHz: nominal 500 kbit/s (32 tq, muestreo 81 %), datos 2 Mbit/s (8 tq, 75 %)

#define FDCAN_MODO_BRS     (1U << 0)  // tramas FD con la fase de datos a 2 Mbit/s
#define FDCAN_MODO_ENLACE  (1U << 1)  // solo acepta 0x700..0x70F y no retransmite solo

#define ENLACE_ID_CMD      0x700U     // maestro -> esclava
#define ENLACE_ID_RESP     0x701U     // esclava -> maestro
#define ENLACE_ID_TEXTO    0x702U     // esclava -> maestro: salida de la consola de la esclava
#define ENLACE_ID_ULTIMO   0x70FU     // el filtro del enlace acepta 0x700..0x70F

volatile uint32_t hse_ok;  // 1 si arrancó el cristal de 16 MHz

typedef struct {
    uint32_t id;
    uint32_t r1;      // word 1 del header: FDF, BRS y DLC
    uint32_t len;     // bytes de datos
    uint8_t data[64];
} can_trama_t;

static const uint8_t dlc_a_len[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64 };

static uint32_t len_a_dlc(uint32_t len)
{
    for (uint32_t d = 0; d < 16; d++) {
        if (dlc_a_len[d] >= len) return d;
    }
    return 15;
}

// Se llama una sola vez, al arrancar: HSE de 16 MHz con fallback a PCLK1 (HSI16)
static void fdcan_reloj_init(void)
{
    RCC_CR |= RCC_HSEON;
    uint32_t t = 100000;
    while (!(RCC_CR & RCC_HSERDY) && --t);
    hse_ok = (t != 0);

    // FDCANSEL: 00 = HSE, 10 = PCLK1. Las dos dan 16 MHz, así que los tiempos de bit no cambian
    RCC_CCIPR = (RCC_CCIPR & ~(3U << 24)) | ((hse_ok ? 0U : 2U) << 24);

    RCC_APB1ENR1 |= RCC_FDCANEN;
    (void)RCC_APB1ENR1;
}

// Deja la instancia en INIT: no transmite ni recibe, no afecta a la otra
static void fdcan_detener(uint32_t base)
{
    FDCAN_CCCR(base) |= CCCR_INIT;
    while (!(FDCAN_CCCR(base) & CCCR_INIT));
}

static void fdcan_iniciar(uint32_t base, uint32_t modo)
{
    fdcan_detener(base);
    FDCAN_CCCR(base) |= CCCR_CCE;

    // Borra solo la message RAM de esta instancia
    volatile uint32_t *ram = (volatile uint32_t *)MRAM(base);
    for (uint32_t i = 0; i < SRAMCAN_SIZE / 4; i++) ram[i] = 0;

    uint32_t cccr = FDCAN_CCCR(base) & ~(CCCR_FDOE | CCCR_BRSE | CCCR_DAR);
    cccr |= CCCR_FDOE;
    if (modo & FDCAN_MODO_BRS) cccr |= CCCR_BRSE;
    if (modo & FDCAN_MODO_ENLACE) cccr |= CCCR_DAR;  // los reintentos los maneja el protocolo
    FDCAN_CCCR(base) = cccr;

    // Nominal 500 kbit/s: 32 tq por bit (1 + 25 + 6), muestreo al 81%
    FDCAN_NBTP(base) = (5U << 25) | (0U << 16) | (24U << 8) | 5U;
    // Datos 2 Mbit/s: 8 tq por bit (1 + 5 + 2), muestreo al 75%, con compensación de retardo
    FDCAN_DBTP(base) = (1U << 23) | (0U << 16) | (4U << 8) | (1U << 4) | 1U;
    FDCAN_TDCR(base) = 6U << 8;  // TDCO = punto de muestreo en tq

    if (modo & FDCAN_MODO_ENLACE) {
        // Un filtro de ID estándar por rango: 0x700..0x70F van al RX FIFO0, el resto se descarta
        volatile uint32_t *f = (volatile uint32_t *)(MRAM(base) + MRAM_FLSSA);
        f[0] = (0U << 30) | (1U << 27) | (ENLACE_ID_CMD << 16) | ENLACE_ID_ULTIMO;
        FDCAN_RXGFC(base) = (1U << 16)   // LSS = 1 filtro estándar
                          | (2U << 4)    // ANFS: rechaza estándar sin filtro
                          | (2U << 2)    // ANFE: rechaza extendidas
                          | 3U;          // rechaza tramas remotas
    } else {
        FDCAN_RXGFC(base) = 0;  // acepta todo en RX FIFO0, como las apps originales
    }

    FDCAN_CCCR(base) &= ~CCCR_INIT;  // arranca (CCE se limpia solo)
    while (FDCAN_CCCR(base) & CCCR_INIT);
}

// Devuelve 1 si la trama quedó en cola, 0 si el TX FIFO está lleno
static int fdcan_enviar(uint32_t base, uint32_t id, const uint8_t *data, uint32_t len, int brs)
{
    if (FDCAN_TXFQS(base) & TXFQS_TFQF) return 0;

    uint32_t dlc = len_a_dlc(len);
    uint32_t idx = (FDCAN_TXFQS(base) >> 16) & 3U;
    volatile uint32_t *e = (volatile uint32_t *)(MRAM(base) + MRAM_TXBUF + idx * MRAM_ELEM);
    e[0] = id << 18;  // ID estándar de 11 bits
    e[1] = ELEM_FDF | (brs ? ELEM_BRS : 0) | (dlc << 16);
    for (uint32_t i = 0; i < (dlc_a_len[dlc] + 3U) / 4; i++) {
        uint32_t w = 0;
        for (uint32_t k = 0; k < 4; k++) {
            uint32_t n = 4 * i + k;
            if (n < len) w |= (uint32_t)data[n] << (8 * k);
        }
        e[2 + i] = w;
    }
    FDCAN_TXBAR(base) = 1U << idx;
    return 1;
}

// Devuelve 1 si sacó una trama del RX FIFO0, 0 si no había nada
static int fdcan_recibir(uint32_t base, can_trama_t *t)
{
    uint32_t s = FDCAN_RXF0S(base);
    if ((s & 0xFU) == 0) return 0;

    uint32_t idx = (s >> 8) & 3U;
    volatile uint32_t *e = (volatile uint32_t *)(MRAM(base) + MRAM_RXF0 + idx * MRAM_ELEM);
    uint32_t r0 = e[0];
    t->r1 = e[1];
    t->id = (r0 & ELEM_XTD) ? (r0 & 0x1FFFFFFFU) | 0x80000000U : (r0 >> 18) & 0x7FFU;  // bit 31 = extendida
    t->len = dlc_a_len[(t->r1 >> 16) & 0xFU];
    for (uint32_t i = 0; i < (t->len + 3) / 4; i++) {
        uint32_t w = e[2 + i];
        for (uint32_t k = 0; k < 4 && 4 * i + k < 64; k++) t->data[4 * i + k] = (uint8_t)(w >> (8 * k));
    }
    FDCAN_RXF0A(base) = idx;  // libera el lugar en el FIFO
    return 1;
}

// CAN1 para las pruebas: transceiver en modo normal, pines en AF9 e instancia andando
static void can1_activar(uint32_t modo)
{
    gpio_salida(CAN_STB_PORT, CAN1_STB, 0);  // STB del TCAN1046 en bajo: modo normal
    gpio_af(GPIOA_BASE, CAN1_RX, 9);
    gpio_af(GPIOA_BASE, CAN1_TX, 9);
    fdcan_iniciar(FDCAN1_BASE, modo);
}

// Estado seguro de CAN1: instancia en INIT, transceiver en standby, pines analógicos. Sin tocar RCC
static void can1_apagar(void)
{
    fdcan_detener(FDCAN1_BASE);
    gpio_poner(CAN_STB_PORT, CAN1_STB, 1);
    gpio_analogico(GPIOA_BASE, CAN1_RX);
    gpio_analogico(GPIOA_BASE, CAN1_TX);
}

// Reinicia el nodo si cayó en bus off. Devuelve PSR (leerlo borra LEC y DLEC)
static uint32_t fdcan_vigilar(uint32_t base, volatile uint32_t *busoff_count)
{
    if (FDCAN_CCCR(base) & CCCR_INIT) {
        FDCAN_CCCR(base) &= ~CCCR_INIT;
        while (FDCAN_CCCR(base) & CCCR_INIT);
        if (busoff_count) (*busoff_count)++;
    }
    return FDCAN_PSR(base);
}
