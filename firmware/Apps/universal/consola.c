// Consola por USART1: TX = PE0 (DO2, J1-1), RX = PE1 (DI2, J1-6), AF7, 8N1
// RX y TX por interrupción con buffers circulares; las líneas se procesan en el loop principal
// Los textos que salen por la consola van sin tildes para que se vean bien en cualquier terminal
// La entrada llega por U5 con flancos lentos: ante ORE, FE, NE o PE se limpia el flag y se descarta el byte
#include <stdarg.h>

#define CONSOLA_BAUD  115200U
#define RX_TAM        128U   // potencias de 2
#define TX_TAM        4096U
#define LINEA_MAX     64U

static volatile uint8_t rx_buf[RX_TAM];
static volatile uint32_t rx_in, rx_out;
static volatile uint8_t tx_buf[TX_TAM];
static volatile uint32_t tx_in, tx_out;

// Para mirar desde el debugger o con "estado"
volatile uint32_t cons_err_rx;    // bytes descartados por ORE, FE, NE o PE
volatile uint32_t cons_perdidos;  // bytes que no entraron en algún buffer

static char linea[LINEA_MAX + 1];
static uint32_t linea_len;
static int linea_desbordada;

// Pasos 2 de la secuencia de arranque: USART1 andando con PE0/PE1 en AF7, el TX queda en reposo alto
static void consola_init(void)
{
    RCC_APB2ENR |= RCC_USART1EN;
    (void)RCC_APB2ENR;
    RCC_CCIPR &= ~3U;  // USART1SEL = 00: reloj de kernel = PCLK2

    USART_CR1(USART1_BASE) = 0;
    USART_BRR(USART1_BASE) = (PCLK2_HZ + CONSOLA_BAUD / 2) / CONSOLA_BAUD;
    USART_CR3(USART1_BASE) = CR3_EIE;  // interrupción también por FE/NE/ORE
    USART_ICR(USART1_BASE) = ICR_ERRORES;
    USART_CR1(USART1_BASE) = CR1_UE | CR1_TE | CR1_RE | CR1_RXNEIE;  // OVER16 y muestreo por mayoría

    gpio_af(CONS_PORT, CONS_TX, 7);
    gpio_af(CONS_PORT, CONS_RX, 7);
    GPIO_PUPDR(CONS_PORT) &= ~(3U << (CONS_RX * 2));  // sin pull: la línea la maneja U5

    nvic_habilitar(USART1_IRQn);
}

void USART1_IRQHandler(void)
{
    uint32_t isr = USART_ISR(USART1_BASE);

    if (isr & (ISR_ORE | ISR_FE | ISR_NE | ISR_PE)) {
        USART_ICR(USART1_BASE) = ISR_ORE | ISR_FE | ISR_NE | ISR_PE;
        if (isr & ISR_RXNE) (void)USART_RDR(USART1_BASE);  // byte con error: se descarta
        cons_err_rx++;
    } else if (isr & ISR_RXNE) {
        uint8_t c = (uint8_t)USART_RDR(USART1_BASE);
        if (rx_in - rx_out < RX_TAM) {
            rx_buf[rx_in % RX_TAM] = c;
            rx_in++;
        } else {
            cons_perdidos++;
        }
    }

    if ((USART_CR1(USART1_BASE) & CR1_TXEIE) && (isr & ISR_TXE)) {
        if (tx_out != tx_in) {
            USART_TDR(USART1_BASE) = tx_buf[tx_out % TX_TAM];
            tx_out++;
        } else {
            USART_CR1(USART1_BASE) &= ~CR1_TXEIE;
        }
    }
}

// Reenvío a la placa MAESTRO: en la ESCLAVA, todo lo que sale por la consola se copia acá y
// enlace.c lo manda por CAN2 (ID 0x702). El maestro lo imprime en su consola con "[esclava]".
// Funciona para cualquier prueba, presente o futura, siempre que imprima con cputc/cputs/cprintf:
// nunca escribir directo en el USART1
#define FWD_TAM       4096U
static uint8_t fwd_buf[FWD_TAM];
static uint32_t fwd_in, fwd_out;
static uint32_t fwd_t;              // cuándo entró el byte más viejo que falta mandar
static uint32_t fwd_lineas;         // '\n' que todavía no salieron
static int consola_reenviar;        // lo prende enlace.c en la ESCLAVA con el enlace activo
volatile uint32_t fwd_perdidos;     // bytes que no entraron en el buffer de reenvío

static void cputc(char c)
{
    if (consola_reenviar) {
        if (fwd_in - fwd_out < FWD_TAM) {
            if (fwd_in == fwd_out) fwd_t = ticks;
            fwd_buf[fwd_in % FWD_TAM] = (uint8_t)c;
            fwd_in++;
            if (c == '\n') fwd_lineas++;
        } else {
            fwd_perdidos++;
        }
    }

    if (tx_in - tx_out >= TX_TAM) {  // lleno: se pierde, nunca se bloquea
        cons_perdidos++;
        return;
    }
    tx_buf[tx_in % TX_TAM] = (uint8_t)c;
    tx_in++;
    __asm volatile ("cpsid i");
    USART_CR1(USART1_BASE) |= CR1_TXEIE;
    __asm volatile ("cpsie i");
}

static void cputs(const char *s)
{
    while (*s) {
        if (*s == '\n') cputc('\r');
        cputc(*s++);
    }
}

// 1 cuando ya salió todo lo que había para mandar
static int consola_tx_vacia(void)
{
    return tx_in == tx_out && (USART_ISR(USART1_BASE) & ISR_TC);
}

static void cput_num(uint32_t v, uint32_t base, int ancho, char relleno, int negativo)
{
    char tmp[12];
    int n = 0;
    do {
        uint32_t d = v % base;
        tmp[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        v /= base;
    } while (v);
    if (negativo) tmp[n++] = '-';
    while (ancho-- > n) cputc(relleno);
    while (n) cputc(tmp[--n]);
}

// printf mínimo: %s %c %d %u %x, con ancho y relleno con 0 (por ejemplo %04u)
static void cprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (*fmt == '\n') cputc('\r');
            cputc(*fmt);
            continue;
        }
        fmt++;
        char relleno = ' ';
        int ancho = 0;
        if (*fmt == '0') { relleno = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') ancho = ancho * 10 + (*fmt++ - '0');
        switch (*fmt) {
        case 's': cputs(va_arg(ap, const char *)); break;
        case 'c': cputc((char)va_arg(ap, int)); break;
        case 'u': cput_num(va_arg(ap, uint32_t), 10, ancho, relleno, 0); break;
        case 'x': cput_num(va_arg(ap, uint32_t), 16, ancho, relleno, 0); break;
        case 'd': {
            int32_t v = va_arg(ap, int32_t);
            cput_num(v < 0 ? (uint32_t)-v : (uint32_t)v, 10, ancho, relleno, v < 0);
            break;
        }
        case '%': cputc('%'); break;
        default: break;
        }
        if (*fmt == '\0') break;
    }
    va_end(ap);
}

// ---------------------------------------------------------------------------------------------
// Ayudas para que las pruebas informen por la consola

static int traza_activa;        // "traza on": cada trama CAN/LIN en hex
static int resumen_activo = 1;  // "resumen off": calla los reportes periódicos (los eventos salen igual)

// Bytes en hex separados por espacios; si son más de 12 muestra los primeros 8 y los últimos 2
static void cput_bytes(const uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (n > 12 && i == 8) {
            cputs(" ..");
            i = n - 2;
        }
        cprintf(" %02x", d[i]);
    }
}

// Como mucho un evento por segundo de cada tipo: si algo falla 10 veces por segundo,
// se ve una línea con el detalle y el resto queda en los contadores del resumen
typedef struct {
    uint32_t t;
    int usado;
} limite_t;

static int limite_ok(limite_t *l)
{
    if (l->usado && (ticks - l->t) < 1000U) return 0;
    l->usado = 1;
    l->t = ticks;
    return 1;
}

// Devuelve la línea completa (sin el fin de línea) o NULL si todavía no hay una. Hace eco
static char *consola_linea(void)
{
    while (rx_out != rx_in) {
        char c = (char)rx_buf[rx_out % RX_TAM];
        rx_out++;

        if (c == '\r' || c == '\n') {
            if (linea_desbordada) {
                cputs("\n(linea demasiado larga, descartada)\n");
                linea_desbordada = 0;
                linea_len = 0;
                continue;
            }
            if (linea_len == 0) continue;  // líneas vacías y el \n de un \r\n
            cputs("\n");
            linea[linea_len] = '\0';
            linea_len = 0;
            return linea;
        }
        if (c == 0x08 || c == 0x7F) {  // borrar
            if (linea_len > 0) {
                linea_len--;
                cputs("\b \b");
            }
            continue;
        }
        if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) continue;  // ruido y caracteres de control
        if (linea_len < LINEA_MAX) {
            linea[linea_len++] = c;
            cputc(c);
        } else {
            linea_desbordada = 1;
        }
    }
    return 0;
}
