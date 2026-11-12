// Firmware Pruebas de ECU32: todo lo de Universal más lo que pide el plan de pruebas
// (plan_pruebas_ecu.pdf). Se graba igual en las dos placas y se maneja igual que Universal:
// consola por J1 (USART1, 115200 8N1) y enlace de control por CAN2
//
// Agrega:
//   es          salidas en nivel o PWM configurable y entradas con frecuencia (DIG-01/02/03)
//   ai          ADC en voltios del conector, promedio de N y VDDA por VREFINT (AI-02)
//   banco_can   CAN1 <-> CAN2 en la misma placa, clásico o FD, N tramas por sentido (COM-02, VR)
//   can_placas  CAN1 y CAN2 entre placas con arbitraje (EXT-02)
//   gateway / gw_fuente   gateway CAN1 clásico -> CAN2 FD, pérdidas y latencia (EXT-03)
//   esp_uart    UART y líneas con la ESP32: eco, 1 MB con CRC, Wi-Fi (ESP-01/02/03)
//   integral    todo a la vez durante N minutos (INT-01/02, PRO-09, EXT-06)
// y los comandos do, pwm, di, frec, promedio, divisor, factor, ref, can_modo, can_tasa,
// can_cantidad, lin_baud, integral, esp, autoarranque y plan ("ayuda" los explica)
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
#include "universal/nucleo_def.c"
#include "universal/enlace.c"

// Pruebas de este firmware, a continuación de las comunes
enum {
    P_ES = P_BASE_TOTAL, P_AI, P_BANCO_CAN, P_CAN_PLACAS, P_GATEWAY, P_GW_FUENTE, P_ESP_UART, P_INTEGRAL,
    P_PRUEBAS_TOTAL
};

// Informe para imprimir más tarde (la esclava espera a que el maestro vuelva al enlace CAN2)
static void (*diferido_fn)(void);
static uint32_t diferido_t, diferido_ms;

static void diferir(void (*fn)(void), uint32_t ms)
{
    diferido_fn = fn;
    diferido_t = ticks;
    diferido_ms = ms;
}

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
#include "pruebas/t_es.c"
#include "pruebas/t_ai.c"
#include "pruebas/t_can.c"
#include "pruebas/t_esp.c"
#include "pruebas/t_integral.c"

static const prueba_t *const tabla[P_PRUEBAS_TOTAL] = {
    [P_LEDS]          = &p_leds,
    [P_DIGITAL]       = &p_digital,
    [P_SALIDAS]       = &p_salidas,
    [P_LOOPBACK]      = &p_loopback,
    [P_ADC]           = &p_adc,
    [P_PWM]           = &p_pwm,
    [P_ESP32]         = &p_esp32,
    [P_CAN_FD]        = &p_can_fd,
    [P_CAN_MAESTRO]   = &p_can_maestro,
    [P_CAN_ESCLAVO]   = &p_can_esclavo,
    [P_WAKEUP]        = &p_wakeup,
    [P_LIN_MAESTRO]   = &p_lin_maestro,
    [P_LIN_ESCLAVO]   = &p_lin_esclavo,
    [P_COMBO_MAESTRO] = &p_combo_maestro,
    [P_COMBO_ESCLAVO] = &p_combo_esclavo,
    [P_ES]            = &p_es,
    [P_AI]            = &p_ai,
    [P_BANCO_CAN]     = &p_banco_can,
    [P_CAN_PLACAS]    = &p_can_placas,
    [P_GATEWAY]       = &p_gateway,
    [P_GW_FUENTE]     = &p_gw_fuente,
    [P_ESP_UART]      = &p_esp_uart,
    [P_INTEGRAL]      = &p_integral,
};

// ---------------------------------------------------------------------------------------------
// Parámetros que se pasan a la esclava por CAN2 (comando 6 del enlace)

enum { PR_CAN_FD = 1, PR_CAN_TASA, PR_CAN_CANTIDAD, PR_LIN_BAUD, PR_IG_MIN, PR_IG_HZ, PR_TOTAL };
#define PARAM_AUTOARRANQUE 8U   // clave de la flash con la prueba que arranca sola al encender

static uint32_t *param_ptr(uint32_t clave)
{
    switch (clave) {
    case PR_CAN_FD:       return &bc_fd;
    case PR_CAN_TASA:     return &bc_tasa;
    case PR_CAN_CANTIDAD: return &bc_cantidad;
    case PR_LIN_BAUD:     return &lin_baud;
    case PR_IG_MIN:       return &ig_min;
    case PR_IG_HZ:        return &ig_hz;
    default:              return 0;
    }
}

static uint32_t param_remoto(uint32_t clave, uint32_t valor)
{
    uint32_t *p = param_ptr(clave);
    if (!p) return ERR_COMANDO;
    *p = valor;
    return ERR_OK;
}

// Cambia un parámetro acá y, si esta placa es MAESTRO con el enlace andando, también en la esclava
static void param_poner(uint32_t clave, uint32_t valor)
{
    *param_ptr(clave) = valor;
    if (rol_actual == ROL_MAESTRO && enlace_activo) enlace_pedir_ex(ENL_CMD_PARAM, clave, valor, ACCION_NINGUNA, 0);
}

static void params_a_la_esclava(void)
{
    for (uint32_t k = PR_CAN_FD; k < PR_TOTAL; k++) enlace_pedir_ex(ENL_CMD_PARAM, k, *param_ptr(k), ACCION_NINGUNA, 0);
}

// ---------------------------------------------------------------------------------------------
// Comandos

// Las salidas las maneja la prueba "es": si no está corriendo, la arranca
static int es_asegurar(void)
{
    if (prueba_actual() == P_INTEGRAL) {
        cputs("La integral esta manejando las salidas: \"parar\" primero\n");
        return 0;
    }
    if (prueba_actual() != P_ES) prueba_correr(P_ES, 0);
    return 1;
}

static void cmd_do(char **pal, uint32_t n)
{
    int32_t s = n > 1 ? texto_numero(pal[1]) : -1;
    int32_t v = n > 2 ? texto_numero(pal[2]) : -1;
    int32_t seg = n > 3 ? texto_numero(pal[3]) : (int32_t)ES_DO2_SEG_DEF;
    if (s < 1 || s > 4 || v < 0 || v > 1 || seg < 1 || seg > (int32_t)ES_DO2_SEG_MAX) {
        cputs("Uso: do <1-4> <0|1> [segundos, solo DO2]\n");
        return;
    }
    if (!es_asegurar()) return;
    es_pwm[s] = 0;
    es_nivel[s] = (uint32_t)v;
    if (s == 2) {
        es_do2_pedir((uint32_t)seg);
    } else {
        es_aplicar((uint32_t)s);
        cprintf("DO%u = %u\n", s, v);
    }
}

static void cmd_pwm(char **pal, uint32_t n)
{
    int32_t s = n > 1 ? texto_numero(pal[1]) : -1;
    if (s >= 1 && s <= 4 && n > 2 && texto_igual(pal[2], "off")) {
        if (!es_asegurar()) return;
        es_pwm[s] = 0;
        es_nivel[s] = 0;
        if (s == 2) es_do2_soltar();
        else es_aplicar((uint32_t)s);
        cprintf("DO%u: PWM apagado, en bajo\n", s);
        return;
    }
    int32_t hz = n > 2 ? texto_numero(pal[2]) : -1;
    int32_t duty = n > 3 ? texto_numero(pal[3]) : 50;
    int32_t seg = n > 4 ? texto_numero(pal[4]) : (int32_t)ES_DO2_SEG_DEF;
    if (s < 1 || s > 4 || hz < 1 || hz > 1000000 || duty < 0 || duty > 100 || seg < 1) {
        cputs("Uso: pwm <1-4> <Hz> [duty %] [segundos, solo DO2]  |  pwm <1-4> off\n");
        return;
    }
    if (!es_asegurar()) return;
    es_pwm[s] = 1;
    es_hz[s] = (uint32_t)hz;
    es_duty[s] = (uint32_t)duty;
    if ((s == 3 && es_pwm[4] && es_hz[4] != (uint32_t)hz) || (s == 4 && es_pwm[3] && es_hz[3] != (uint32_t)hz)) {
        es_hz[3] = es_hz[4] = (uint32_t)hz;
        cputs("Aviso: DO3 y DO4 comparten el TIM8, quedan las dos a la misma frecuencia\n");
    }
    if (s == 2) {
        es_do2_pedir((uint32_t)seg);
    } else {
        es_aplicar((uint32_t)s);
        cprintf("DO%u: PWM %u Hz, %u %%\n", s, hz, duty);
    }
}

static void cmd_divisor(char **pal, uint32_t n)
{
    int32_t c = n > 1 ? texto_numero(pal[1]) : -1;
    int32_t rs = n > 2 ? texto_numero(pal[2]) : -1;
    int32_t rg = n > 3 ? texto_numero(pal[3]) : -1;
    if (c < 1 || c > 4 || rs < 0 || rg < 1) {
        cputs("Uso: divisor <1-4> <R serie en ohm> <R a GND en ohm>   (ej: divisor 4 100000 15000)\n");
        return;
    }
    uint32_t f = (uint32_t)(((uint64_t)(uint32_t)rs + (uint32_t)rg) * 1000U / (uint32_t)rg);
    if (f > 0xFFFFU || !param_guardar(AI_PARAM_FACTOR + (uint32_t)c - 1U, f)) {
        cputs("ERROR: factor fuera de rango o no se pudo grabar la flash\n");
        return;
    }
    ai_cargar_factores();
    cprintf("AN%u: factor %u.%03u (fondo de escala %u mV en el conector), guardado\n", c, f / 1000U, f % 1000U,
            (uint32_t)(3300ULL * f / 1000U));
}

static void cmd_factor(void)
{
    ai_cargar_factores();
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t p = param_leer(AI_PARAM_FACTOR + i);
        cprintf("AN%u: factor %u.%03u (%s)\n", i + 1, ai_factor[i] / 1000U, ai_factor[i] % 1000U,
                p == PARAM_VACIO ? "nominal: fondo de escala / 3,3 V" : "medido, cargado con \"divisor\"");
    }
}

static void cmd_ref(char **pal, uint32_t n)
{
    int32_t c = n > 1 ? texto_numero(pal[1]) : -1;
    int32_t ref = n > 2 ? texto_numero(pal[2]) : -1;
    if (c < 1 || c > 4 || ref < 0) {
        cputs("Uso: ref <1-4> <mV medidos con el multimetro en el conector>\n");
        return;
    }
    if (!adc_lecturas) {
        cputs("Primero \"correr ai\" para que el ADC este midiendo\n");
        return;
    }
    uint32_t i = (uint32_t)c - 1U;
    uint32_t med = ai_mv_conector(i);
    uint32_t dif = med > (uint32_t)ref ? med - (uint32_t)ref : (uint32_t)ref - med;
    uint32_t pct = (uint32_t)(((uint64_t)dif * 10000U + ai_fs_mv[i] / 2) / ai_fs_mv[i]);  // centésimas de %
    cprintf("AN%u: ADC %u mV, multimetro %u mV, error %u mV = %u.%02u %% FS -> %s (criterio: <= 1 %% FS)\n",
            c, med, ref, dif, pct / 100U, pct % 100U, pct <= 100U ? "PASA" : "FALLA");
}

static void cmd_autoarranque(const char *arg)
{
    if (!arg) {
        uint32_t v = param_leer(PARAM_AUTOARRANQUE);
        if (v == PARAM_VACIO || !v || v >= P_PRUEBAS_TOTAL) cputs("Autoarranque: ninguno\n");
        else cprintf("Autoarranque: %s\n", tabla[v]->nombre);
        return;
    }
    uint32_t p = texto_igual(arg, "no") ? 0 : buscar_prueba(arg);
    if (!p && !texto_igual(arg, "no")) {
        cputs("Uso: autoarranque <prueba|no>\n");
        return;
    }
    if (param_guardar(PARAM_AUTOARRANQUE, p)) {
        if (p) cprintf("Al encender arranca sola: %s\n", tabla[p]->nombre);
        else cputs("Autoarranque desactivado\n");
    } else {
        cputs("ERROR al grabar la flash\n");
    }
}

static void cmd_plan(void)
{
    cputs("PLAN DE PRUEBAS -> que correr (detalle en firmware/README.md)\n"
          "  ALI-03     grabar y depurar por SWD este firmware (breakpoint en main)\n"
          "  DIG-01     do <1|3|4> 1  /  do <n> 0. DO2 en reposo esta en alto; \"do 2 0 [s]\" la baja s segundos\n"
          "  DIG-02     correr es (avisa cada cambio de entrada) y di; rampa lenta para el umbral\n"
          "  DIG-03     pwm <n> 100000 50 en cada salida; frec on mide la frecuencia de las entradas\n"
          "  AI-02      correr ai; divisor <c> <Rs> <Rg>; ref <c> <mV del multimetro> da el error en % FS\n"
          "  COM-02     puente CAN1-CAN2 en J3: can_modo clasico | fd; can_cantidad 100000; correr banco_can\n"
          "  COM-03     LIN entre placas: par lin (esta placa commander) y par lin_inv (al reves)\n"
          "  INT-01     puente CAN1-CAN2 y LIN a la otra placa: integral 10 [todas]; autoarranque integral\n"
          "  INT-02     integral con can_tasa max (carga de bus alta) y medir la corriente\n"
          "  EXT-01     repetir todo en la placa B y comparar\n"
          "  EXT-02     CAN1-CAN1 y CAN2-CAN2 entre placas: can_modo fd; par canx\n"
          "  EXT-03     mismo cableado: par gateway (la fuente informa perdidas y latencia)\n"
          "  EXT-04     par lin y par lin_inv 30 min cada uno; estado da los errores por tipo\n"
          "  EXT-06     integral 60 con el gabinete cerrado\n"
          "  ESP-01/03  correr esp_uart; esp wifi on (medir rieles y consumo)\n"
          "  ESP-02     esp eco; esp enviar; esp recibir; esp baud 921600 y repetir; esp lineas\n"
          "  VR         CAN: can_cantidad 1000 y correr banco_can. LIN: estado despues de 1000 respuestas\n"
          "  PRO-00/09  ai (ADC a 50 % FS), do (VOH a 4 mA), integral\n"
          "  Sin firmware especial: DOC, INS, ALI-01/02, AI-01, COM-01, EXT-05, ESP-04 y las mediciones de PRO\n");
}

static void ayuda_extra(void)
{
    cputs("\n"
          "PLAN DE PRUEBAS (firmware Pruebas): \"plan\" dice que correr para cada prueba del plan\n"
          "  do <1-4> <0|1> [s]          fija una salida (DO2 es el TX de la consola: se presta s segundos)\n"
          "  pwm <1-4> <Hz> [duty] [s]   PWM en una salida (DO3 y DO4 comparten frecuencia); pwm <n> off\n"
          "  di                          nivel de las 4 entradas\n"
          "  frec on|off                 frecuencia de las entradas (cuenta flancos 1 s)\n"
          "  promedio <1-256>            muestras promediadas por canal del ADC (256 por defecto)\n"
          "  divisor <c> <Rs> <Rg>       carga el divisor de AN<c> en ohm (queda en la flash); factor: los muestra\n"
          "  ref <c> <mV>                error del ADC contra el multimetro, en % del fondo de escala\n"
          "  can_modo clasico|fd         CAN clasico 8 bytes o FD 64 bytes con BRS (banco_can, can_placas, integral)\n"
          "  can_tasa <n|max>            tramas/s por sentido; max = lo mas rapido posible\n"
          "  can_cantidad <n>            tramas por sentido antes de terminar (0 = sin fin)\n"
          "  lin_baud <n>                velocidad LIN (20000 por defecto en este firmware)\n"
          "  integral [min] [todas]      todo a la vez min minutos (todas = tambien PWM en DO2)\n"
          "  esp <...>                   enlace con la ESP32: ping, baud, eco, enviar, recibir, lineas, reset, wifi\n"
          "  autoarranque <prueba|no>    prueba que arranca sola al encender (queda en la flash)\n"
          "  par canx | gateway | lin_inv   (MAESTRO) CAN entre placas, gateway, LIN con los roles invertidos\n"
          "  esclava sync                (MAESTRO) le pasa a la esclava can_*, lin_baud e integral\n"
          "  Los can_*, lin_baud e integral que se cambian en el MAESTRO se copian solos a la esclava\n");
}

static int comando_extra(char **pal, uint32_t n)
{
    const char *c = pal[0];
    if (texto_igual(c, "esclava")) {  // "esclava ..." que el núcleo no conoce
        if (n > 1 && texto_igual(pal[1], "sync")) {
            params_a_la_esclava();
            cputs("Parametros enviados a la esclava\n");
            return 1;
        }
        return 0;
    }
    if (texto_igual(c, "do")) cmd_do(pal, n);
    else if (texto_igual(c, "pwm")) cmd_pwm(pal, n);
    else if (texto_igual(c, "di")) {
        es_imprimir_di(es_leer_di());
        cputs("\n");
    } else if (texto_igual(c, "frec")) {
        int on = n > 1 ? texto_on_off(pal[1]) : -1;
        if (on < 0) cputs("Uso: frec on|off\n");
        else if (on == 0) es_frec_activar(0);
        else if (es_asegurar()) {
            es_frec_activar(1);
            cputs("frec: midiendo DI1..DI4 en ventanas de 1 s (a 100 kHz, de a una entrada por vez)\n");
        }
    } else if (texto_igual(c, "promedio")) {
        int32_t v = n > 1 ? texto_numero(pal[1]) : -1;
        if (v < 1 || v > 256) cputs("Uso: promedio <1-256>\n");
        else {
            adc_promedio = (uint32_t)v;
            cprintf("ADC: promedio de %u muestras por canal\n", v);
        }
    } else if (texto_igual(c, "divisor")) cmd_divisor(pal, n);
    else if (texto_igual(c, "factor")) cmd_factor();
    else if (texto_igual(c, "ref")) cmd_ref(pal, n);
    else if (texto_igual(c, "can_modo")) {
        if (n > 1 && texto_igual(pal[1], "fd")) param_poner(PR_CAN_FD, 1);
        else if (n > 1 && (texto_igual(pal[1], "clasico") || texto_igual(pal[1], "classic"))) param_poner(PR_CAN_FD, 0);
        else {
            cputs("Uso: can_modo clasico|fd\n");
            return 1;
        }
        cprintf("CAN: %s\n", bc_fd ? "FD 500 kbit/s / 2 Mbit/s, 64 bytes con BRS" : "clasico 500 kbit/s, 8 bytes");
    } else if (texto_igual(c, "can_tasa")) {
        int32_t v = (n > 1 && texto_igual(pal[1], "max")) ? 0 : (n > 1 ? texto_numero(pal[1]) : -1);
        if (v < 0 || v > 20000) cputs("Uso: can_tasa <tramas por segundo por sentido | max>\n");
        else {
            param_poner(PR_CAN_TASA, (uint32_t)v);
            if (v) cprintf("CAN: %u tramas/s por sentido\n", v);
            else cputs("CAN: a la tasa maxima\n");
        }
    } else if (texto_igual(c, "can_cantidad")) {
        int32_t v = n > 1 ? texto_numero(pal[1]) : -1;
        if (v < 0) cputs("Uso: can_cantidad <tramas por sentido, 0 = sin fin>\n");
        else {
            param_poner(PR_CAN_CANTIDAD, (uint32_t)v);
            cprintf("CAN: %u tramas por sentido%s\n", v, v ? "" : " (sin fin)");
        }
    } else if (texto_igual(c, "lin_baud")) {
        int32_t v = n > 1 ? texto_numero(pal[1]) : -1;
        if (v < 1000 || v > 20000) cprintf("Uso: lin_baud <1000-20000> (ahora %u)\n", lin_baud);
        else {
            param_poner(PR_LIN_BAUD, (uint32_t)v);
            cprintf("LIN: %u baud (se aplica al arrancar la prueba LIN)\n", v);
        }
    } else if (texto_igual(c, "integral")) {
        int32_t m = n > 1 ? texto_numero(pal[1]) : -1;
        if (m >= 0) param_poner(PR_IG_MIN, (uint32_t)m);
        ig_todas = (n > 2 && texto_igual(pal[2], "todas")) || (n > 1 && texto_igual(pal[1], "todas"));
        prueba_correr(P_INTEGRAL, 0);
    } else if (texto_igual(c, "esp")) {
        if (prueba_actual() != P_ESP_UART) prueba_correr(P_ESP_UART, 0);
        if (n > 1) eu_comando(pal, n);
    } else if (texto_igual(c, "autoarranque")) cmd_autoarranque(n > 1 ? pal[1] : 0);
    else if (texto_igual(c, "plan")) cmd_plan();
    else return 0;
    return 1;
}

static int par_extra(const char *arg)
{
    uint32_t suya, mia;
    if (texto_igual(arg, "canx")) { suya = P_CAN_PLACAS; mia = P_CAN_PLACAS; }
    else if (texto_igual(arg, "gateway")) { suya = P_GW_FUENTE; mia = P_GATEWAY; }
    else if (texto_igual(arg, "lin_inv")) {
        params_a_la_esclava();
        if (enlace_pedir(ENL_CMD_CORRER, P_LIN_MAESTRO, ACCION_NINGUNA)) prueba_correr(P_LIN_ESCLAVO, 0);
        return 1;
    } else return 0;

    // La esclava arranca primero; esta placa arranca cuando llega su ACK (las dos apagan el enlace)
    params_a_la_esclava();
    prueba_parar();
    enlace_pedir_ex(ENL_CMD_CORRER, suya, 0, ACCION_PAR_LOCAL, mia);
    cprintf("par %s: la esclava corre %s y esta placa %s; el enlace CAN2 se apaga mientras dura y "
            "al terminar llega el informe de las dos\n", arg, tabla[suya]->nombre, tabla[mia]->nombre);
    return 1;
}

static void arranque_extra(int retomo_prueba)
{
    // Lo que pide el plan: ADC con 256 muestras y VDDA medida, LIN a 20 kbit/s
    adc_usar_vref = 1;
    adc_promedio = 256;
    lin_baud = 20000;
    if (retomo_prueba) return;
    uint32_t v = param_leer(PARAM_AUTOARRANQUE);
    if (v != PARAM_VACIO && v && v < P_PRUEBAS_TOTAL) {
        cprintf("Autoarranque: %s (\"autoarranque no\" lo desactiva)\n", tabla[v]->nombre);
        prueba_correr(v, 0);
    }
}

static void loop_extra(void)
{
    if (diferido_fn && (ticks - diferido_t) >= diferido_ms) {
        void (*fn)(void) = diferido_fn;
        diferido_fn = 0;
        fn();
    }
}

#include "universal/nucleo.c"

int main(void)
{
    nucleo_main(tabla, P_PRUEBAS_TOTAL, "Pruebas");
    return 0;
}
