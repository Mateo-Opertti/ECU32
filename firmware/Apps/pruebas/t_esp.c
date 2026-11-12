// Prueba "esp_uart": enlace STM32 <-> ESP32 del zócalo (plan ESP-01, ESP-02 y ESP-03)
// Hace falta el sketch ESP32/ESP32_Pruebas/ESP32_Pruebas.ino en la ESP32
// USART2: TX = PD5 (RX2 / GPIO16 de la ESP32), RX = PD6 (TX2 / GPIO17). Arranca a 115200
// Líneas auxiliares: PD4 <-> GPIO4 (D4), PD3 <-> GPIO18 (D18), PE7 -> EN (reset de la ESP32)
// Comandos ("esp ..."):
//   ping                 la ESP32 contesta PONG
//   baud <n>             cambia la velocidad de los dos lados (115200, 921600, ...)
//   eco [n]              n líneas de texto de ida y vuelta, comparadas carácter a carácter
//   enviar [bytes]       STM32 -> ESP32: datos pseudoaleatorios y CRC-32 al final (1 MB por defecto)
//   recibir [bytes]      ESP32 -> STM32: ídem en el otro sentido, además verificados byte a byte
//   lineas               cada línea auxiliar en los dos sentidos y en los dos niveles
//   reset                pulso en EN (PE7, open-drain): la ESP32 rearranca y vuelve a 115200
//   wifi on|off          la ESP32 transmite por Wi-Fi sin parar (para ESP-01 y ESP-03)
// El CRC-32 (polinomio 0x04C11DB7, inicial 0xFFFFFFFF, sin reflejar) lo calcula la unidad CRC del STM32

#define EU_RX_TAM        4096U
#define EU_TX_TAM        128U
#define EU_LIN_TAM       96U
#define EU_CR1_OVER8     (1U << 15)
#define EU_CR1_FIFOEN    (1U << 29)
#define EU_RX_PIN        6U      // PD6, AF7 (USART2_RX)
#define EU_D4_PIN        4U      // PD4 <-> GPIO4
#define EU_D18_PIN       3U      // PD3 <-> GPIO18
#define EU_EN_PIN        7U      // PE7 -> EN
#define EU_SEMILLA       0x12345678U
#define EU_BYTES_DEF     1048576U
#define EU_ECO_DEF       100U

static volatile uint8_t eu_rx[EU_RX_TAM];
static volatile uint32_t eu_rx_in, eu_rx_out;
static volatile uint32_t eu_rx_err, eu_rx_perdidos;
static uint32_t eu_baud;
static char eu_tx[EU_TX_TAM];
static uint32_t eu_tx_len, eu_tx_i;
static char eu_lin[EU_LIN_TAM];
static uint32_t eu_lin_len;

enum { OP_NADA, OP_PING, OP_BAUD, OP_ECO, OP_ENVIAR, OP_RECIBIR, OP_LINEAS, OP_RESET, OP_WIFI };
static const char *const eu_op_nombre[] = { "", "ping", "baud", "eco", "enviar", "recibir", "lineas", "reset", "wifi" };
static uint32_t eu_op, eu_fase, eu_t, eu_t_ini;
static uint32_t eu_n, eu_i, eu_ok, eu_mal, eu_rng, eu_baud_nuevo;
static char eu_esperado[EU_LIN_TAM];
static volatile uint32_t eu_total_ok, eu_total_err;

// ---------------------------------------------------------------------------------------------
// USART2

void USART2_IRQHandler(void)
{
    uint32_t isr = USART_ISR(USART2_BASE);
    if (isr & (ISR_ORE | ISR_FE | ISR_NE | ISR_PE)) {
        USART_ICR(USART2_BASE) = ISR_ORE | ISR_FE | ISR_NE | ISR_PE;
        eu_rx_err++;
    }
    while (USART_ISR(USART2_BASE) & ISR_RXNE) {  // con FIFO: mientras quede algo
        uint8_t c = (uint8_t)USART_RDR(USART2_BASE);
        if (eu_rx_in - eu_rx_out < EU_RX_TAM) {
            eu_rx[eu_rx_in % EU_RX_TAM] = c;
            eu_rx_in++;
        } else {
            eu_rx_perdidos++;
        }
    }
}

// OVER8 para que 921600 baud quede con poco error (-0,8 % con 16 MHz)
static void eu_baud_poner(uint32_t b)
{
    uint32_t div = (2U * PCLK1_HZ + b / 2) / b;
    USART_CR1(USART2_BASE) &= ~CR1_UE;
    USART_BRR(USART2_BASE) = (div & ~0xFU) | ((div & 0xFU) >> 1);
    USART_CR1(USART2_BASE) |= CR1_UE;
    eu_baud = b;
}

static void eu_hw_init(void)
{
    RCC_APB1ENR1 |= RCC_USART2EN;
    (void)RCC_APB1ENR1;
    USART_CR1(USART2_BASE) = 0;
    USART_CR3(USART2_BASE) = CR3_EIE;
    USART_ICR(USART2_BASE) = ICR_ERRORES;
    USART_CR1(USART2_BASE) = EU_CR1_FIFOEN | EU_CR1_OVER8 | CR1_RXNEIE | CR1_TE | CR1_RE;
    eu_baud_poner(115200U);
    gpio_af(ESP_PORT, ESP_RX2, 7);
    gpio_af(ESP_PORT, EU_RX_PIN, 7);
    gpio_entrada(GPIOD_BASE, EU_D4_PIN, 0);
    gpio_entrada(GPIOD_BASE, EU_D18_PIN, 0);
    eu_rx_in = eu_rx_out = 0;
    nvic_habilitar(USART2_IRQn);
}

static void eu_hw_deinit(void)
{
    nvic_deshabilitar(USART2_IRQn);
    USART_CR1(USART2_BASE) = 0;
    RCC_APB1ENR1 &= ~RCC_USART2EN;
    gpio_analogico(ESP_PORT, ESP_RX2);
    gpio_analogico(ESP_PORT, EU_RX_PIN);
    gpio_analogico(GPIOD_BASE, EU_D4_PIN);
    gpio_analogico(GPIOD_BASE, EU_D18_PIN);
}

static void eu_mandar(const char *s)
{
    eu_tx_len = 0;
    while (*s && eu_tx_len < EU_TX_TAM) eu_tx[eu_tx_len++] = *s++;
    eu_tx_i = 0;
}

static void eu_tx_step(void)
{
    while (eu_tx_i < eu_tx_len && (USART_ISR(USART2_BASE) & ISR_TXE)) USART_TDR(USART2_BASE) = (uint8_t)eu_tx[eu_tx_i++];
}

static void eu_vaciar_rx(void)
{
    eu_rx_out = eu_rx_in;
    eu_lin_len = 0;
}

// Línea completa recibida (sin \r\n) o NULL
static char *eu_linea(void)
{
    while (eu_rx_out != eu_rx_in) {
        char c = (char)eu_rx[eu_rx_out % EU_RX_TAM];
        eu_rx_out++;
        if (c == '\n') {
            eu_lin[eu_lin_len] = '\0';
            eu_lin_len = 0;
            return eu_lin;
        }
        if (c != '\r' && eu_lin_len < EU_LIN_TAM - 1) eu_lin[eu_lin_len++] = c;
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Ayudas

static uint8_t eu_rng_byte(void)  // xorshift32, igual que en el sketch de la ESP32
{
    eu_rng ^= eu_rng << 13;
    eu_rng ^= eu_rng >> 17;
    eu_rng ^= eu_rng << 5;
    return (uint8_t)eu_rng;
}

static void eu_crc_reset(void)
{
    RCC_AHB1ENR |= RCC_CRCEN;
    (void)RCC_AHB1ENR;
    CRC_CR = 1U;  // RESET: vuelve al valor inicial; polinomio y tamaño por defecto (CRC-32)
}

static int eu_empieza(const char *s, const char *p)
{
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}

static uint32_t eu_hex(const char *s)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < 8 && *s; i++, s++) {
        char c = *s;
        uint32_t d = (c >= '0' && c <= '9') ? (uint32_t)(c - '0') : (c >= 'A' && c <= 'F') ? (uint32_t)(c - 'A' + 10)
                   : (c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10) : 0;
        v = (v << 4) | d;
    }
    return v;
}

// Escribe "texto + número + resto" en buf
static void eu_armar(char *buf, const char *a, uint32_t num, const char *b)
{
    uint32_t k = 0;
    while (*a && k < EU_LIN_TAM - 12) buf[k++] = *a++;
    char tmp[11];
    int n = 0;
    do { tmp[n++] = (char)('0' + num % 10); num /= 10; } while (num);
    while (n) buf[k++] = tmp[--n];
    while (*b && k < EU_LIN_TAM - 2) buf[k++] = *b++;
    buf[k] = '\0';
}

static void eu_empezar(uint32_t op)
{
    eu_op = op;
    eu_fase = 0;
    eu_t = eu_t_ini = ticks;
    eu_ok = eu_mal = eu_i = 0;
}

static void eu_terminar(int ok)
{
    if (ok) eu_total_ok++;
    else eu_total_err++;
    eu_op = OP_NADA;
}

// ---------------------------------------------------------------------------------------------
// Líneas auxiliares

enum { LP_STM_SALIDA, LP_STM_ENTRADA, LP_STM_LEER, LP_ESP_LEER, LP_ESP_PIN, LP_ESP_SUELTA };
typedef struct { uint8_t tipo, stm_pin, gpio, valor; const char *nombre; } eu_paso_t;
static const eu_paso_t eu_pasos[] = {
    { LP_STM_SALIDA, EU_D4_PIN, 0, 1, 0 },   { LP_ESP_LEER, 0, 4, 1, "PD4 -> GPIO4 en alto" },
    { LP_STM_SALIDA, EU_D4_PIN, 0, 0, 0 },   { LP_ESP_LEER, 0, 4, 0, "PD4 -> GPIO4 en bajo" },
    { LP_STM_ENTRADA, EU_D4_PIN, 0, 0, 0 },
    { LP_STM_SALIDA, EU_D18_PIN, 0, 1, 0 },  { LP_ESP_LEER, 0, 18, 1, "PD3 -> GPIO18 en alto" },
    { LP_STM_SALIDA, EU_D18_PIN, 0, 0, 0 },  { LP_ESP_LEER, 0, 18, 0, "PD3 -> GPIO18 en bajo" },
    { LP_STM_ENTRADA, EU_D18_PIN, 0, 0, 0 },
    { LP_ESP_PIN, 0, 4, 1, 0 },              { LP_STM_LEER, EU_D4_PIN, 0, 1, "GPIO4 -> PD4 en alto" },
    { LP_ESP_PIN, 0, 4, 0, 0 },              { LP_STM_LEER, EU_D4_PIN, 0, 0, "GPIO4 -> PD4 en bajo" },
    { LP_ESP_SUELTA, 0, 4, 0, 0 },
    { LP_ESP_PIN, 0, 18, 1, 0 },             { LP_STM_LEER, EU_D18_PIN, 0, 1, "GPIO18 -> PD3 en alto" },
    { LP_ESP_PIN, 0, 18, 0, 0 },             { LP_STM_LEER, EU_D18_PIN, 0, 0, "GPIO18 -> PD3 en bajo" },
    { LP_ESP_SUELTA, 0, 18, 0, 0 },
};
#define EU_N_PASOS (sizeof(eu_pasos) / sizeof(eu_pasos[0]))

static void eu_lineas_resultado(const char *nombre, int ok)
{
    if (ok) eu_ok++;
    else eu_mal++;
    cprintf("esp lineas: %s %s\n", nombre, ok ? "OK" : "FALLA");
}

// Ejecuta el paso eu_i. eu_fase: 0 = por empezar, 1 = esperando respuesta de la ESP32
static void eu_lineas_step(void)
{
    if (eu_i >= EU_N_PASOS) {
        cprintf("esp lineas: %u OK, %u con falla -> %s\n", eu_ok, eu_mal, eu_mal ? "FALLA" : "PASA");
        eu_terminar(!eu_mal);
        return;
    }
    const eu_paso_t *p = &eu_pasos[eu_i];
    char buf[EU_LIN_TAM];
    if (eu_fase == 0 && (ticks - eu_t) < 10U) return;  // deja asentar la línea anterior
    switch (p->tipo) {
    case LP_STM_SALIDA:
        gpio_salida(GPIOD_BASE, p->stm_pin, p->valor);
        eu_i++;
        eu_t = ticks;
        break;
    case LP_STM_ENTRADA:
        gpio_entrada(GPIOD_BASE, p->stm_pin, 0);
        eu_i++;
        eu_t = ticks;
        break;
    case LP_STM_LEER:
        eu_lineas_resultado(p->nombre, gpio_leer(GPIOD_BASE, p->stm_pin) == p->valor);
        eu_i++;
        eu_t = ticks;
        break;
    default:  // pasos que le piden algo a la ESP32
        if (eu_fase == 0) {
            if (p->tipo == LP_ESP_LEER) eu_armar(buf, "LEER ", p->gpio, "\n");
            else if (p->tipo == LP_ESP_PIN) eu_armar(buf, "PIN ", p->gpio, p->valor ? " 1\n" : " 0\n");
            else eu_armar(buf, "SUELTA ", p->gpio, "\n");
            eu_mandar(buf);
            eu_fase = 1;
            eu_t = ticks;
        } else if ((ticks - eu_t) > 500U) {
            if (p->nombre) eu_lineas_resultado(p->nombre, 0);
            else cprintf("esp lineas: la ESP32 no contesto (paso %u)\n", eu_i);
            eu_i++;
            eu_fase = 0;
            eu_t = ticks;
        }
        break;
    }
}

static void eu_lineas_linea(const char *l)
{
    const eu_paso_t *p = &eu_pasos[eu_i];
    if (eu_fase != 1) return;
    if (p->tipo == LP_ESP_LEER && eu_empieza(l, "NIVEL ")) {
        char c = l[0];
        while (*l) c = *l++;  // el último carácter es el nivel
        eu_lineas_resultado(p->nombre, (uint32_t)(c - '0') == p->valor);
    } else if (!eu_empieza(l, "OK")) {
        return;
    }
    eu_i++;
    eu_fase = 0;
    eu_t = ticks;
}

// ---------------------------------------------------------------------------------------------
// Operaciones

static void eu_eco_siguiente(void)
{
    eu_armar(eu_esperado, "ECO ", eu_i, " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghij");
    char buf[EU_LIN_TAM];
    uint32_t k = 0;
    while (eu_esperado[k]) { buf[k] = eu_esperado[k]; k++; }
    buf[k++] = '\n';
    buf[k] = '\0';
    eu_mandar(buf);
    eu_t = ticks;
}

static void eu_linea_recibida(const char *l)
{
    switch (eu_op) {
    case OP_PING:
        if (eu_empieza(l, "PONG")) {
            cprintf("esp ping: PONG a %u baud (%u ms)\n", eu_baud, ticks - eu_t_ini);
            eu_terminar(1);
        }
        break;
    case OP_BAUD:
        if (eu_fase == 0 && eu_empieza(l, "OK")) {
            eu_fase = 1;  // la ESP32 cambia después de mandar el OK
            eu_t = ticks;
        } else if (eu_fase == 2 && eu_empieza(l, "PONG")) {
            cprintf("esp baud: los dos a %u baud, PING / PONG OK\n", eu_baud);
            eu_terminar(1);
        }
        break;
    case OP_ECO:
        if (eu_empieza(l, "ECO ")) {
            const char *a = l, *b = eu_esperado;
            while (*a && *a == *b) { a++; b++; }
            if (*a == *b) eu_ok++;
            else eu_mal++;
            if (++eu_i < eu_n) eu_eco_siguiente();
            else {
                cprintf("esp eco: %u de %u lineas iguales a %u baud -> %s\n", eu_ok, eu_n, eu_baud, eu_mal ? "FALLA" : "PASA");
                eu_terminar(!eu_mal);
            }
        }
        break;
    case OP_ENVIAR:
        if (eu_fase == 0 && eu_empieza(l, "LISTO")) {
            eu_crc_reset();
            eu_rng = EU_SEMILLA;
            eu_fase = 1;
            eu_t = ticks;
        } else if (eu_fase == 2 && eu_empieza(l, "CRC ")) {
            uint32_t suyo = eu_hex(l + 4), mio = CRC_DR;
            uint32_t ms = ticks - eu_t_ini;
            cprintf("esp enviar: %u bytes STM32 -> ESP32 a %u baud en %u ms (%u B/s), CRC STM32 %08x ESP32 %08x -> %s\n",
                    eu_n, eu_baud, ms, ms ? (uint32_t)((uint64_t)eu_n * 1000U / ms) : 0, mio, suyo,
                    mio == suyo ? "PASA" : "FALLA");
            eu_terminar(mio == suyo);
        }
        break;
    case OP_RECIBIR:
        if (eu_fase == 2 && eu_empieza(l, "CRC ")) {
            uint32_t suyo = eu_hex(l + 4), mio = CRC_DR;
            uint32_t ms = ticks - eu_t_ini;
            int ok = mio == suyo && !eu_mal;
            cprintf("esp recibir: %u bytes ESP32 -> STM32 a %u baud en %u ms (%u B/s), bytes distintos %u, "
                    "CRC STM32 %08x ESP32 %08x -> %s\n", eu_n, eu_baud, ms,
                    ms ? (uint32_t)((uint64_t)eu_n * 1000U / ms) : 0, eu_mal, mio, suyo, ok ? "PASA" : "FALLA");
            eu_terminar(ok);
        }
        break;
    case OP_LINEAS:
        eu_lineas_linea(l);
        break;
    case OP_RESET:
        if (eu_fase == 1 && eu_empieza(l, "ESP32 LISTO")) {
            cprintf("esp reset: la ESP32 rearranco en %u ms y contesto a 115200\n", ticks - eu_t);
            eu_terminar(1);
        }
        break;
    case OP_WIFI:
        if (eu_empieza(l, "OK")) {
            cputs("esp wifi: OK\n");
            eu_terminar(1);
        }
        break;
    default:
        cprintf("[esp32] %s\n", l);  // algo que la ESP32 mandó sin que se le pidiera
        break;
    }
}

static void eu_step(void)
{
    eu_tx_step();

    // ENVIAR: hasta 64 bytes por vuelta, mientras el FIFO de TX tenga lugar
    if (eu_op == OP_ENVIAR && eu_fase == 1) {
        for (uint32_t k = 0; k < 64 && eu_i < eu_n && (USART_ISR(USART2_BASE) & ISR_TXE); k++) {
            uint8_t b = eu_rng_byte();
            USART_TDR(USART2_BASE) = b;
            CRC_DR8 = b;
            eu_i++;
            eu_t = ticks;
        }
        if (eu_i >= eu_n) {
            eu_fase = 2;
            eu_t = ticks;
        }
    }

    // RECIBIR: los datos llegan en binario; se verifican y se pasan por el CRC
    if (eu_op == OP_RECIBIR && eu_fase == 1) {
        for (uint32_t k = 0; k < 512 && eu_rx_out != eu_rx_in && eu_i < eu_n; k++) {
            uint8_t b = eu_rx[eu_rx_out % EU_RX_TAM];
            eu_rx_out++;
            if (b != eu_rng_byte()) eu_mal++;
            CRC_DR8 = b;
            eu_i++;
            eu_t = ticks;
        }
        if (eu_i >= eu_n) {
            eu_fase = 2;
            eu_t = ticks;
        }
    } else {
        char *l = eu_linea();
        if (l) eu_linea_recibida(l);
    }

    switch (eu_op) {
    case OP_BAUD:
        if (eu_fase == 1 && (ticks - eu_t) >= 50U) {
            eu_baud_poner(eu_baud_nuevo);
            eu_vaciar_rx();
            eu_mandar("PING\n");
            eu_fase = 2;
            eu_t = ticks;
        }
        break;
    case OP_LINEAS:
        eu_lineas_step();
        break;
    case OP_RESET:
        if (eu_fase == 0 && (ticks - eu_t) >= 100U) {
            GPIO_MODER(GPIOE_BASE) &= ~(3U << (EU_EN_PIN * 2));  // suelta EN: el pull-up del DevKit la arranca
            eu_baud_poner(115200U);
            eu_vaciar_rx();
            eu_fase = 1;
            eu_t = ticks;
        }
        break;
    case OP_ECO:
        if ((ticks - eu_t) > 500U) {  // esta línea no volvió
            eu_mal++;
            if (++eu_i < eu_n) eu_eco_siguiente();
            else {
                cprintf("esp eco: %u de %u lineas iguales -> FALLA\n", eu_ok, eu_n);
                eu_terminar(0);
            }
        }
        break;
    default:
        break;
    }

    // Tiempo máximo de cada espera
    uint32_t limite = (eu_op == OP_RESET) ? 4000U : (eu_op == OP_ENVIAR || eu_op == OP_RECIBIR) ? 3000U : 1000U;
    if (eu_op != OP_NADA && eu_op != OP_ECO && eu_op != OP_LINEAS && (ticks - eu_t) > limite) {
        cprintf("esp %s: la ESP32 no contesto (fase %u, %u de %u bytes). Esta el sketch ESP32_Pruebas? "
                "Si quedo en otra velocidad, \"esp reset\"\n", eu_op_nombre[eu_op], eu_fase, eu_i, eu_n);
        if (eu_op == OP_BAUD) eu_baud_poner(115200U);
        eu_terminar(0);
    }
}

// Comando "esp ...". pal[0] = "esp"
static void eu_comando(char **pal, uint32_t n)
{
    const char *sub = n > 1 ? pal[1] : 0;
    if (!sub) {
        cputs("Uso: esp ping | baud <n> | eco [n] | enviar [bytes] | recibir [bytes] | lineas | reset | wifi on|off\n");
        return;
    }
    if (eu_op != OP_NADA) {
        cprintf("esp: todavia esta en curso \"%s\"\n", eu_op_nombre[eu_op]);
        return;
    }
    int32_t v = n > 2 ? texto_numero(pal[2]) : -1;
    char buf[EU_LIN_TAM];
    eu_vaciar_rx();
    if (texto_igual(sub, "ping")) {
        eu_empezar(OP_PING);
        eu_mandar("PING\n");
    } else if (texto_igual(sub, "baud")) {
        if (v < 9600) {
            cputs("Uso: esp baud <9600..1000000> (el plan pide 115200 y 921600)\n");
            return;
        }
        eu_empezar(OP_BAUD);
        eu_baud_nuevo = (uint32_t)v;
        eu_armar(buf, "BAUD ", eu_baud_nuevo, "\n");
        eu_mandar(buf);
    } else if (texto_igual(sub, "eco")) {
        eu_empezar(OP_ECO);
        eu_n = v > 0 ? (uint32_t)v : EU_ECO_DEF;
        eu_eco_siguiente();
    } else if (texto_igual(sub, "enviar") || texto_igual(sub, "recibir")) {
        int enviar = texto_igual(sub, "enviar");
        eu_empezar(enviar ? OP_ENVIAR : OP_RECIBIR);
        eu_n = v > 0 ? (uint32_t)v : EU_BYTES_DEF;
        eu_armar(buf, enviar ? "RX " : "TX ", eu_n, "\n");
        if (!enviar) {
            eu_crc_reset();
            eu_rng = EU_SEMILLA;
            eu_fase = 1;  // lo que llegue después del pedido son los datos
        }
        eu_mandar(buf);
        cprintf("esp %s: %u bytes a %u baud...\n", sub, eu_n, eu_baud);
    } else if (texto_igual(sub, "lineas")) {
        eu_empezar(OP_LINEAS);
    } else if (texto_igual(sub, "reset")) {
        eu_empezar(OP_RESET);
        gpio_poner(GPIOE_BASE, EU_EN_PIN, 0);
        GPIO_OTYPER(GPIOE_BASE) |= 1U << EU_EN_PIN;  // open-drain: el DevKit tiene su propio pull-up
        GPIO_MODER(GPIOE_BASE) = (GPIO_MODER(GPIOE_BASE) & ~(3U << (EU_EN_PIN * 2))) | (1U << (EU_EN_PIN * 2));
    } else if (texto_igual(sub, "wifi") && n > 2) {
        int on = texto_on_off(pal[2]);
        if (on < 0) {
            cputs("Uso: esp wifi on|off\n");
            return;
        }
        eu_empezar(OP_WIFI);
        eu_mandar(on ? "WIFI ON\n" : "WIFI OFF\n");
    } else {
        cputs("Uso: esp ping | baud <n> | eco [n] | enviar [bytes] | recibir [bytes] | lineas | reset | wifi on|off\n");
    }
}

static void eu_init(void)
{
    eu_hw_init();
    eu_op = OP_NADA;
    eu_tx_len = eu_tx_i = 0;
    eu_rx_err = eu_rx_perdidos = 0;
    eu_total_ok = eu_total_err = 0;
    cputs("esp_uart: USART2 a 115200. Probar con \"esp ping\" (la ESP32 necesita el sketch ESP32_Pruebas)\n");
}

static void eu_deinit(void)
{
    eu_hw_deinit();
}

static void eu_estado(void)
{
    cprintf("  %u baud, operacion %s, bytes con error %u, perdidos %u\n", eu_baud,
            eu_op ? eu_op_nombre[eu_op] : "ninguna", eu_rx_err, eu_rx_perdidos);
}

static const prueba_t p_esp_uart = {
    .nombre = "esp_uart", .archivo = "ESP_UART",
    .descripcion = "UART y lineas STM32 <-> ESP32: eco, 1 MB con CRC, lineas, reset, Wi-Fi (ESP-01/02/03)",
    .init = eu_init, .step = eu_step, .deinit = eu_deinit, .estado = eu_estado,
    .ok = &eu_total_ok, .err = &eu_total_err,
};
