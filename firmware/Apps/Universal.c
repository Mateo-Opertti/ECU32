// Firmware Universal de ECU32: un solo programa, grabado igual en las dos placas, que corre
// cualquiera de las pruebas de bring-up y se maneja por consola serie o desde la otra placa por CAN2
//
// Consola: USART1 a 115200 8N1 por J1: TX = DO2 (J1-1, PE0) y RX = DI2 (J1-6, PE1), a través de U5
// Enlace de control: FDCAN2 (CAN2 de J3). El MAESTRO le manda comandos a la ESCLAVA
// Rol: se guarda en la última página de flash (rol.c). Sin rol grabado, la placa es ESCLAVA
//
// Cada prueba es un módulo con init(), step() no bloqueante y deinit(). El loop principal atiende
// la consola, el enlace CAN2 y la prueba que esté corriendo, así que nada se queda esperando
// Los programas sueltos anteriores están en Apps/legacy/ y se pueden seguir grabando desde Src/main.c
// STM32G491VC con el reloj por defecto (HSI16 = 16 MHz)
#include "ecu32_placa.h"

volatile uint32_t ticks;  // ms desde el arranque

// La llama el startup antes de main
void SystemInit(void)
{
    SCB_CPACR |= (0xFU << 20);  // habilita la FPU
}

void SysTick_Handler(void) { ticks++; }

#include "universal/consola.c"
#include "universal/fdcan.c"
#include "universal/rol.c"

// ---------------------------------------------------------------------------------------------
// Pruebas

#define PRUEBA_SOLO_LOCAL (1U << 0)  // solo desde la consola de la placa, nunca por CAN2

typedef struct {
    const char *nombre;       // nombre corto para la consola
    const char *archivo;      // nombre del programa original (también se acepta en "correr")
    const char *descripcion;
    void (*init)(void);
    void (*step)(void);       // no bloqueante: lo llama el loop principal
    void (*deinit)(void);     // apaga lo propio de la prueba; DO1/DO3/DO4 y LEDs los apaga el loop
    void (*estado)(void);     // detalles para "estado" (puede ser NULL)
    volatile uint32_t *ok;    // contadores de tramas o lecturas (pueden ser NULL)
    volatile uint32_t *err;
    uint32_t flags;
} prueba_t;

enum {
    P_NINGUNA, P_LEDS, P_DIGITAL, P_SALIDAS, P_LOOPBACK, P_ADC, P_PWM, P_ESP32,
    P_CAN_FD, P_CAN_MAESTRO, P_CAN_ESCLAVO, P_WAKEUP, P_LIN_MAESTRO, P_LIN_ESCLAVO,
    P_COMBO_MAESTRO, P_COMBO_ESCLAVO, P_TOTAL
};

// Implementadas más abajo; las usan el enlace y algunas pruebas
static int prueba_correr(uint32_t n, int desde_can);
static void prueba_parar(void);
static uint32_t prueba_actual(void);
static const char *prueba_nombre(uint32_t n);
static uint32_t prueba_segundos(void);
static int prueba_contadores(uint32_t *ok, uint32_t *err);
static void par_wakeup_ack(void);

// Parámetros que antes se cambiaban desde el Watch
static volatile uint32_t pwm_duty = 50;  // %, comando "duty"
static volatile uint32_t adc_canal;      // 0 = AN1 ... 3 = AN4, comando "canal"

// 1 cuando la placa arrancó porque la despertó un WKUP mientras corría CAN_Esclavo_WakeUp
static int wakeup_desperto;
#define WK_MARCA      0xEC320000U  // TAMP_BKP0R = WK_MARCA | número de prueba, antes de entrar en Standby
#define WK_MARCA_MASK 0xFFFF0000U

#include "universal/enlace.c"
#include "universal/p_leds.c"
#include "universal/p_digital.c"
#include "universal/p_digital_salidas.c"
#include "universal/p_digital_loopback.c"
#include "universal/p_adc.c"
#include "universal/p_pwm.c"
#include "universal/p_esp32_serial.c"
#include "universal/p_can_fd.c"
#include "universal/p_can_maestro.c"
#include "universal/p_can_esclavo.c"
#include "universal/p_can_wakeup.c"
#include "universal/p_lin.c"
#include "universal/p_lin_maestro.c"
#include "universal/p_lin_esclavo.c"
#include "universal/p_combo.c"

static const prueba_t *const pruebas[P_TOTAL] = {
    [P_LEDS]     = &p_leds,
    [P_DIGITAL]  = &p_digital,
    [P_SALIDAS]  = &p_salidas,
    [P_LOOPBACK] = &p_loopback,
    [P_ADC]      = &p_adc,
    [P_PWM]      = &p_pwm,
    [P_ESP32]    = &p_esp32,
    [P_CAN_FD]      = &p_can_fd,
    [P_CAN_MAESTRO] = &p_can_maestro,
    [P_CAN_ESCLAVO] = &p_can_esclavo,
    [P_WAKEUP]      = &p_wakeup,
    [P_LIN_MAESTRO] = &p_lin_maestro,
    [P_LIN_ESCLAVO] = &p_lin_esclavo,
    [P_COMBO_MAESTRO] = &p_combo_maestro,
    [P_COMBO_ESCLAVO] = &p_combo_esclavo,
};

static uint32_t actual;     // prueba corriendo (P_NINGUNA = ninguna)
static uint32_t t_inicio;   // ticks cuando arrancó

// ---------------------------------------------------------------------------------------------
// Placa

// Secuencia de arranque, antes de cualquier prueba (reemplaza "Buffer de E/S" de las notas de arranque):
// 1) DO1, DO3 y DO4 en bajo  2) consola andando, TX en reposo alto  3) bajar 1/OE  4) bajar 2/OE
static void placa_arranque(void)
{
    RCC_AHB2ENR |= RCC_GPIOAEN | RCC_GPIOBEN | RCC_GPIOCEN | RCC_GPIODEN | RCC_GPIOEEN | RCC_GPIOFEN;
    RCC_APB1ENR1 |= RCC_PWREN | RCC_RTCAPBEN;
    (void)RCC_APB1ENR1;  // espera a que se habiliten los relojes

    gpio_salida(DO1_PORT, DO1, 0);
    gpio_salida(DO3_PORT, DO3, 0);
    gpio_salida(DO4_PORT, DO4, 0);
    consola_init();
    gpio_salida(OE_PORT, OE1, 0);
    gpio_salida(OE_PORT, OE2, 0);

    // Con 2/OE en bajo U5 maneja DI1, DI3 y DI4 todo el tiempo: tienen que quedar como entradas
    gpio_entrada(DI1_PORT, DI1, 0);
    gpio_entrada(DI3_PORT, DI3, 0);
    gpio_entrada(DI4_PORT, DI4, 0);

    gpio_salida(LED_PORT, LED_ROJO, 0);
    gpio_salida(LED_PORT, LED_VERDE, 0);

    gpio_salida(CAN_STB_PORT, CAN1_STB, 1);  // CAN1 en standby hasta que una prueba lo use
    gpio_salida(LIN_PORT, LIN_EN, 0);        // LIN en sleep

    PWR_CR3 &= ~(WKUP2 | WKUP3);  // los pines de wake-up vuelven a ser GPIO comunes
}

// Lo que queda después de cada prueba. No toca la consola ni los /OE
static void placa_salidas_seguras(void)
{
    gpio_salida(DO1_PORT, DO1, 0);
    gpio_salida(DO3_PORT, DO3, 0);
    gpio_salida(DO4_PORT, DO4, 0);
    leds(0, 0);
    gpio_poner(CAN_STB_PORT, CAN1_STB, 1);
    gpio_poner(LIN_PORT, LIN_EN, 0);
}

// ---------------------------------------------------------------------------------------------
// Manejo de pruebas

static uint32_t prueba_actual(void) { return actual; }

static const char *prueba_nombre(uint32_t n)
{
    return (n > P_NINGUNA && n < P_TOTAL && pruebas[n]) ? pruebas[n]->nombre : "?";
}

static uint32_t prueba_segundos(void)
{
    return actual ? (ticks - t_inicio) / 1000 : 0;
}

static int prueba_contadores(uint32_t *ok, uint32_t *err)
{
    if (!actual || !pruebas[actual]->ok) return 0;
    *ok = *pruebas[actual]->ok;
    *err = pruebas[actual]->err ? *pruebas[actual]->err : 0;
    return 1;
}

static void prueba_parar(void)
{
    if (!actual) return;
    pruebas[actual]->deinit();
    placa_salidas_seguras();
    cprintf("Parada: %s\n", pruebas[actual]->nombre);
    actual = P_NINGUNA;
}

// Devuelve un código de error del enlace (ERR_OK si arrancó)
static int prueba_correr(uint32_t n, int desde_can)
{
    if (n <= P_NINGUNA || n >= P_TOTAL || !pruebas[n]) return ERR_PRUEBA;
    if (desde_can && (pruebas[n]->flags & PRUEBA_SOLO_LOCAL)) return ERR_SOLO_LOCAL;

    prueba_parar();
    actual = n;
    t_inicio = ticks;
    cprintf("Corriendo %u: %s\n", n, pruebas[n]->nombre);
    pruebas[n]->init();
    return ERR_OK;
}

// ---------------------------------------------------------------------------------------------
// Comando "par": arranca la versión Maestro acá y le pide la versión Esclavo a la otra placa

#define PAR_WAKEUP_ESPERA_MS 3500  // la esclava muestra el rojo 3 s antes de dormirse

static int par_pendiente;
static uint32_t par_t;

static void par_wakeup_ack(void)
{
    par_pendiente = 1;
    par_t = ticks;
    cputs("La esclava se duerme en 3 s; CAN_Maestro arranca aca en 3,5 s\n");
}

static void par_step(void)
{
    if (par_pendiente && (ticks - par_t) >= PAR_WAKEUP_ESPERA_MS) {
        par_pendiente = 0;
        prueba_correr(P_CAN_MAESTRO, 0);
    }
}

// ---------------------------------------------------------------------------------------------
// Consola

static int texto_igual(const char *a, const char *b)  // sin distinguir mayúsculas
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
        if (!x) return 1;
    }
}

// Devuelve el número o -1 si no es un número decimal
static int32_t texto_numero(const char *s)
{
    int32_t v = 0;
    if (!*s) return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || v > 100000) return -1;
        v = v * 10 + (*s - '0');
    }
    return v;
}

// Acepta el número, el nombre corto o el nombre del programa original. 0 si no existe
static uint32_t buscar_prueba(const char *s)
{
    if (!s) return P_NINGUNA;
    int32_t n = texto_numero(s);
    if (n > P_NINGUNA && n < P_TOTAL && pruebas[n]) return (uint32_t)n;
    for (uint32_t i = P_NINGUNA + 1; i < P_TOTAL; i++) {
        if (pruebas[i] && (texto_igual(s, pruebas[i]->nombre) || texto_igual(s, pruebas[i]->archivo))) return i;
    }
    return P_NINGUNA;
}

static void cmd_ayuda(void)
{
    cputs("Comandos (una linea y Enter; mayusculas o minusculas da igual)\n"
          "\n"
          "PRUEBAS EN ESTA PLACA\n"
          "  lista                       pruebas numeradas\n"
          "  correr <n|nombre>           para la prueba actual y arranca otra (ej: correr 9, correr can_maestro)\n"
          "  parar                       para la prueba y deja la placa en estado seguro\n"
          "  estado                      rol, prueba, tiempo, contadores, estado del bus y opciones\n"
          "  duty <0-100>                duty del PWM en % (prueba pwm)\n"
          "  canal <1-4>                 entrada AN que muestra el ADC en los LEDs (prueba adc)\n"
          "\n"
          "QUE INFORMAN LAS PRUEBAS POR ESTA CONSOLA\n"
          "  resumen on|off              reporte cada 1 s: tramas/s, OK, errores y estado del bus en CAN y LIN,\n"
          "                              voltios en adc, niveles en digital/salidas/loopback. Arranca en on\n"
          "  traza on|off                ademas, cada trama CAN y LIN en hex (para depurar). Arranca en off\n"
          "  Los avisos de fallas salen siempre, apenas pasan: sin respuesta, trama mal (y que byte),\n"
          "  checksum mal, eco mal, error pasivo, BUS OFF... (como mucho uno por segundo de cada tipo)\n"
          "  Sin argumento, \"resumen\" y \"traza\" muestran como estan\n"
          "\n"
          "ROL Y LA OTRA PLACA (por CAN2)\n"
          "  rol [maestro|esclava]       muestra o cambia el rol (queda guardado en la flash)\n"
          "  esclava correr <n|nombre>   (MAESTRO) arranca una prueba en la esclava\n"
          "  esclava parar               (MAESTRO) para la prueba de la esclava\n"
          "  esclava estado              (MAESTRO) rol, prueba, tiempo y contadores de la esclava\n"
          "  esclava resumen on|off      (MAESTRO) prende o apaga el resumen en la esclava\n"
          "  esclava traza on|off        (MAESTRO) prende o apaga la traza en la esclava\n"
          "  par <can|lin|combo|wakeup>  (MAESTRO) version Maestro aca y version Esclavo en la otra placa\n"
          "                              combo: cada placa le pasa sus AN a la otra por LIN y los LEDs de\n"
          "                              cada una muestran la entrada de la OTRA (elegida con \"canal\")\n"
          "  Todo lo que imprime la esclava aparece en esta consola con \"[esclava]\"\n"
          "\n"
          "EJEMPLO: CAN entre placas mirando las dos puntas\n"
          "  par can                     arranca las dos; cada 1 s llega el resumen de esta placa y de la esclava\n"
          "  traza on                    ademas muestra cada trama de esta placa\n"
          "  traza off / resumen off     calla la consola para escribir tranquilo (las fallas siguen saliendo)\n"
          "  parar  y  esclava parar     termina\n");
}

static void cmd_lista(void)
{
    for (uint32_t i = P_NINGUNA + 1; i < P_TOTAL; i++) {
        if (!pruebas[i]) continue;
        cprintf("%2u  %s", i, pruebas[i]->nombre);
        uint32_t largo = 0;
        while (pruebas[i]->nombre[largo]) largo++;
        while (largo++ < 15) cputc(' ');
        cputs(pruebas[i]->descripcion);
        if (pruebas[i]->flags & PRUEBA_SOLO_LOCAL) cputs(" [solo local]");
        cputs("\n");
    }
}

static void cmd_estado(void)
{
    cprintf("Rol: %s\n", rol_nombre(rol_actual));
    if (!actual) {
        cputs("Prueba: ninguna\n");
    } else {
        cprintf("Prueba: %u %s (%s), corriendo %u s\n", actual, pruebas[actual]->nombre,
                pruebas[actual]->archivo, prueba_segundos());
        uint32_t ok, err;
        if (prueba_contadores(&ok, &err)) cprintf("  OK %u  error %u\n", ok, err);
        if (pruebas[actual]->estado) pruebas[actual]->estado();
    }
    cprintf("Enlace CAN2: %s, bus off %u, pedidos sin respuesta %u\n",
            enlace_activo ? "activo" : "suspendido", enl_busoff, enl_sin_resp);
    cprintf("Reloj FDCAN: %s\n", hse_ok ? "HSE 16 MHz" : "PCLK1 (no arranco el HSE)");
    cprintf("Consola: resumen %s, traza %s; %u bytes con error descartados, %u perdidos\n",
            resumen_activo ? "on" : "off", traza_activa ? "on" : "off", cons_err_rx, cons_perdidos);
    if (rol_actual == ROL_ESCLAVA) cprintf("Reenvio al maestro por CAN2: %u bytes perdidos\n", fwd_perdidos);
    else cprintf("Texto de la esclava: %u tramas perdidas\n", txt_perdidas);
}

static void cmd_rol(const char *arg)
{
    if (!arg) {
        cprintf("Rol: %s\n", rol_nombre(rol_actual));
        return;
    }
    uint32_t r;
    if (texto_igual(arg, "maestro")) r = ROL_MAESTRO;
    else if (texto_igual(arg, "esclava") || texto_igual(arg, "esclavo")) r = ROL_ESCLAVA;
    else {
        cputs("Uso: rol [maestro|esclava]\n");
        return;
    }
    if (rol_guardar(r)) cprintf("Rol guardado: %s\n", rol_nombre(rol_actual));
    else cprintf("ERROR al grabar la flash. Rol actual: %s\n", rol_nombre(rol_actual));
}

// "on" -> 1, "off" -> 0, otra cosa -> -1
static int texto_on_off(const char *s)
{
    if (s && texto_igual(s, "on")) return 1;
    if (s && texto_igual(s, "off")) return 0;
    return -1;
}

// "resumen on|off" y "traza on|off" en esta placa
static void cmd_on_off(const char *nombre, int *opcion, const char *arg)
{
    if (!arg) {
        cprintf("%s: %s (cambiar con \"%s on\" o \"%s off\")\n", nombre, *opcion ? "on" : "off", nombre, nombre);
        return;
    }
    int v = texto_on_off(arg);
    if (v < 0) {
        cprintf("Uso: %s on|off\n", nombre);
        return;
    }
    *opcion = v;
    cprintf("%s %s\n", nombre, v ? "on" : "off");
}

static void cmd_esclava(const char *sub, const char *arg)
{
    if (sub && (texto_igual(sub, "traza") || texto_igual(sub, "resumen"))) {
        int v = texto_on_off(arg);
        if (v < 0) {
            cprintf("Uso: esclava %s on|off\n", sub);
            return;
        }
        enlace_pedir(texto_igual(sub, "traza") ? ENL_CMD_TRAZA : ENL_CMD_RESUMEN, (uint32_t)v, ACCION_NINGUNA);
    } else if (sub && texto_igual(sub, "correr")) {
        uint32_t n = buscar_prueba(arg);
        if (!n) {
            cputs("Prueba desconocida (ver \"lista\")\n");
            return;
        }
        enlace_pedir(ENL_CMD_CORRER, n, ACCION_NINGUNA);
    } else if (sub && texto_igual(sub, "parar")) {
        enlace_pedir(ENL_CMD_PARAR, 0, ACCION_NINGUNA);
    } else if (sub && texto_igual(sub, "estado")) {
        enlace_pedir(ENL_CMD_ESTADO, 0, ACCION_NINGUNA);
    } else {
        cputs("Uso: esclava correr <n|nombre> | parar | estado | resumen on|off | traza on|off\n");
    }
}

static void cmd_par(const char *arg)
{
    if (rol_actual != ROL_MAESTRO) {
        cputs("\"par\" se usa desde el MAESTRO (ver \"rol\")\n");
        return;
    }
    if (arg && texto_igual(arg, "can")) {
        if (enlace_pedir(ENL_CMD_CORRER, P_CAN_ESCLAVO, ACCION_NINGUNA)) prueba_correr(P_CAN_MAESTRO, 0);
    } else if (arg && texto_igual(arg, "lin")) {
        if (enlace_pedir(ENL_CMD_CORRER, P_LIN_ESCLAVO, ACCION_NINGUNA)) prueba_correr(P_LIN_MAESTRO, 0);
    } else if (arg && texto_igual(arg, "combo")) {
        if (enlace_pedir(ENL_CMD_CORRER, P_COMBO_ESCLAVO, ACCION_NINGUNA)) prueba_correr(P_COMBO_MAESTRO, 0);
    } else if (arg && texto_igual(arg, "wakeup")) {
        // CAN_Maestro arranca recién cuando la esclava confirma y se duerme
        prueba_parar();
        enlace_pedir(ENL_CMD_CORRER, P_WAKEUP, ACCION_PAR_WAKEUP);
    } else {
        cputs("Uso: par <can|lin|combo|wakeup>\n");
    }
}

static void comando(char *l)
{
    char *pal[4] = { 0, 0, 0, 0 };
    uint32_t n = 0;
    while (*l && n < 4) {
        while (*l == ' ' || *l == '\t') *l++ = '\0';
        if (!*l) break;
        pal[n++] = l;
        while (*l && *l != ' ' && *l != '\t') l++;
    }
    if (!pal[0]) return;

    // Cualquier comando que cambie la prueba cancela un "par wakeup" en espera
    if (texto_igual(pal[0], "correr") || texto_igual(pal[0], "parar") || texto_igual(pal[0], "par")) par_pendiente = 0;

    if (texto_igual(pal[0], "ayuda") || texto_igual(pal[0], "?")) {
        cmd_ayuda();
    } else if (texto_igual(pal[0], "lista")) {
        cmd_lista();
    } else if (texto_igual(pal[0], "correr")) {
        uint32_t p = buscar_prueba(pal[1]);
        if (p) prueba_correr(p, 0);
        else cputs("Prueba desconocida (ver \"lista\")\n");
    } else if (texto_igual(pal[0], "parar")) {
        if (actual) prueba_parar();
        else cputs("No hay ninguna prueba corriendo\n");
    } else if (texto_igual(pal[0], "estado")) {
        cmd_estado();
    } else if (texto_igual(pal[0], "rol")) {
        cmd_rol(pal[1]);
    } else if (texto_igual(pal[0], "esclava")) {
        cmd_esclava(pal[1], pal[2]);
    } else if (texto_igual(pal[0], "par")) {
        cmd_par(pal[1]);
    } else if (texto_igual(pal[0], "resumen")) {
        cmd_on_off("resumen", &resumen_activo, pal[1]);
    } else if (texto_igual(pal[0], "traza")) {
        cmd_on_off("traza", &traza_activa, pal[1]);
    } else if (texto_igual(pal[0], "duty")) {
        int32_t d = pal[1] ? texto_numero(pal[1]) : -1;
        if (d < 0 || d > 100) {
            cputs("Uso: duty <0-100>\n");
        } else {
            pwm_duty = (uint32_t)d;
            cprintf("Duty del PWM: %u %%%s\n", pwm_duty, actual == P_PWM ? "" : " (se aplica cuando corra pwm)");
        }
    } else if (texto_igual(pal[0], "canal")) {
        int32_t c = pal[1] ? texto_numero(pal[1]) : -1;
        if (c < 1 || c > 4) {
            cputs("Uso: canal <1-4>\n");
        } else {
            adc_canal = (uint32_t)c - 1;
            cprintf("LEDs del ADC: AN%u\n", c);
        }
    } else {
        cprintf("Comando desconocido: %s (ver \"ayuda\")\n", pal[0]);
    }
}

// ---------------------------------------------------------------------------------------------
// Arranque

static void imprimir_causa(uint32_t csr, uint32_t sr1)
{
    cputs("Reset:");
    if (sr1 & SR1_SBF) {
        cputs(" salio de Standby");
        if (sr1 & WKUP3) cputs(", lo desperto WKUP3 (CAN1)");
        if (sr1 & WKUP2) cputs(", lo desperto WKUP2 (CAN2)");
        if (!(sr1 & (WKUP2 | WKUP3))) cputs(" por un reset, no por wake-up");
    }
    if (csr & CSR_LPWRRSTF) cputs(" bajo-consumo");
    if (csr & CSR_WWDGRSTF) cputs(" WWDG");
    if (csr & CSR_IWDGRSTF) cputs(" IWDG");
    if (csr & CSR_SFTRSTF)  cputs(" software");
    if (csr & CSR_BORRSTF)  cputs(" encendido/BOR");
    if (csr & CSR_PINRSTF)  cputs(" pin-NRST");
    if (csr & CSR_OBLRSTF)  cputs(" option-bytes");
    cprintf("  (RCC_CSR = 0x%08x, PWR_SR1 = 0x%03x)\n", csr, sr1);
}

int main(void)
{
    // SysTick a 1 ms con el reloj del CPU
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 0x7;

    placa_arranque();

    // Causa del reset y prueba a retomar: las banderas y TAMP sobreviven al reset y al Standby
    uint32_t csr = RCC_CSR, sr1 = PWR_SR1, marca = TAMP_BKP0R;
    RCC_CSR |= CSR_RMVF;
    PWR_SCR = SCR_TODO;
    int hay_marca = (marca & WK_MARCA_MASK) == WK_MARCA;
    if (hay_marca) {
        PWR_CR1 |= CR1_DBP;
        TAMP_BKP0R = 0;
        PWR_CR1 &= ~CR1_DBP;
    }

    rol_leer();
    fdcan_reloj_init();
    enlace_init();

    cputs("\n\nECU32 - firmware Universal\n");
    imprimir_causa(csr, sr1);
    cprintf("Rol: %s. Escribi \"ayuda\" para ver los comandos\n", rol_nombre(rol_actual));

    if (hay_marca) {
        uint32_t n = marca & 0xFFU;
        if ((sr1 & SR1_SBF) && (sr1 & (WKUP2 | WKUP3))) {
            wakeup_desperto = 1;
            prueba_correr(n, 0);
        } else {
            cputs("La placa se reseteo mientras dormia: no se retoma la prueba\n");
        }
    }

    for (;;) {
        char *l = consola_linea();
        if (l) comando(l);
        enlace_step();
        if (actual) pruebas[actual]->step();
        par_step();
    }
}
