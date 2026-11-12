// CAN_Esclavo_WakeUp: la placa se duerme en Standby y la despierta la actividad en el bus
// Al arrancar la prueba: LED rojo (PF9) prendido 3 s, se apaga y el MCU entra en Standby
//   (esos 3 s son para ver que arrancó, para mandar "parar" o para reprogramarla: dormida no
//   responde al ST-LINK ni a la consola)
// Cuando llega algo por CAN1: el TCAN1046 detecta el patrón de wake-up (WUP) y baja RXD1,
//   que entra por R61 a PE6 (WKUP3) y despierta al MCU
// También la despierta un comando del maestro por CAN2: RXD2 entra a PC13 (WKUP2). El primer
//   intento del maestro se pierde mientras la placa arranca y el reintento recibe el ACK
// Standby borra la RAM y despierta con un reset. Antes de dormir se guarda en TAMP_BKP0R qué prueba
//   corría; al despertar el arranque imprime la causa (SBF, WUFx) y vuelve a esta prueba, ya despierta:
//   el verde (PF10) alterna cada 250 ms
// Si en cambio la placa se resetea mientras duerme (botón de reset), arranca sin prueba
// No usa el FDCAN: no recibe ni confirma tramas, solo prueba el camino bus -> transceiver -> WKUP
// Mientras duerme, los /OE vuelven a alto por R53/R54 y la consola se corta; el arranque la recupera

#define WK_AVISO_MS     3000U
#define WK_NORMAL_MS    2U     // un rato en modo normal: el transceiver olvida el wake-up anterior
#define WK_STANDBY_MS   10U    // espera a que RXD quede en alto ya en standby
#define WK_PARPADEO_MS  250U
#define WK_TEXTO_MS     200U   // como mucho, espera para que el texto pendiente salga por CAN2

enum { WK_AVISO, WK_NORMAL, WK_STANDBY, WK_DESPIERTA, WK_TEXTO };

static uint32_t wk_estado, wk_t;
static volatile uint32_t wk_no_durmio;  // veces que el wfi volvió sin dormir (hubo actividad justo antes)

static void wk_init(void)
{
    wk_t = ticks;
    if (wakeup_desperto) {
        wakeup_desperto = 0;
        wk_estado = WK_DESPIERTA;
        leds(0, 0);
        cputs("wakeup: desperto. El verde alterna cada 250 ms\n");
    } else {
        wk_estado = WK_AVISO;
        leds(1, 0);
        cputs("wakeup: rojo 3 s y despues entra en Standby. La despierta CAN1 (WKUP3) o un comando por CAN2 (WKUP2)\n");
    }
}

// Si el Standby funciona no vuelve: al despertar el MCU arranca de nuevo desde el reset
static void wk_dormir(void)
{
    PWR_CR1 |= CR1_DBP;
    TAMP_BKP0R = WK_MARCA | P_WAKEUP;
    PWR_CR1 &= ~CR1_DBP;

    PWR_CR4 |= WKUP3 | WKUP2;  // flanco descendente: el WUP baja RXD
    PWR_CR3 |= WKUP3 | WKUP2;  // habilita WKUP3 (PE6) y WKUP2 (PC13)
    PWR_CR1 = (PWR_CR1 & ~CR1_LPMS_MASK) | CR1_LPMS_STANDBY;
    SCB_SCR |= SCR_SLEEPDEEP;

    SYST_CSR = 0;                     // sin esto una interrupción del SysTick cancelaría el wfi
    nvic_deshabilitar(USART1_IRQn);   // ídem con un byte que llegue por la consola
    PWR_SCR = SCR_TODO;               // las banderas se limpian al final, con los WKUP ya configurados

    __asm volatile ("dsb");
    __asm volatile ("wfi");

    // Llegar acá significa que hubo actividad justo antes de dormir: cuenta como wake-up
    SCB_SCR &= ~SCR_SLEEPDEEP;
    PWR_CR1 &= ~CR1_LPMS_MASK;
    PWR_CR3 &= ~(WKUP2 | WKUP3);
    SYST_CVR = 0;
    SYST_CSR = 0x7;
    nvic_habilitar(USART1_IRQn);
    PWR_CR1 |= CR1_DBP;
    TAMP_BKP0R = 0;
    PWR_CR1 &= ~CR1_DBP;
    gpio_poner(CAN_STB_PORT, CAN2_STB, 0);  // el enlace vuelve a escuchar

    wk_no_durmio++;
    wk_estado = WK_DESPIERTA;
    wk_t = ticks;
    cputs("wakeup: no llego a dormirse (hubo actividad justo antes); sigue como despierta\n");
}

static void wk_step(void)
{
    switch (wk_estado) {
    case WK_AVISO:
        if ((ticks - wk_t) >= WK_AVISO_MS) {
            leds(0, 0);
            cputs("wakeup: entrando en Standby (la consola se corta hasta que despierte)\n");
            wk_estado = WK_TEXTO;
            wk_t = ticks;
        }
        break;
    case WK_TEXTO:
        // En la ESCLAVA, deja salir por CAN2 lo que falta mostrar en el maestro antes de apagar CAN2
        if (enlace_texto_vacio() || (ticks - wk_t) >= WK_TEXTO_MS) {
            gpio_poner(CAN_STB_PORT, CAN1_STB, 0);  // pasa por modo normal...
            wk_estado = WK_NORMAL;
            wk_t = ticks;
        }
        break;
    case WK_NORMAL:
        if ((ticks - wk_t) >= WK_NORMAL_MS) {
            gpio_poner(CAN_STB_PORT, CAN1_STB, 1);  // ...y vuelve a standby: RXD queda en alto hasta un WUP
            gpio_poner(CAN_STB_PORT, CAN2_STB, 1);  // CAN2 también en standby, para WKUP2
            wk_estado = WK_STANDBY;
            wk_t = ticks;
        }
        break;
    case WK_STANDBY:
        if ((ticks - wk_t) >= WK_STANDBY_MS && consola_tx_vacia()) wk_dormir();
        break;
    default:  // WK_DESPIERTA
        if ((ticks - wk_t) >= WK_PARPADEO_MS) {
            wk_t += WK_PARPADEO_MS;
            led_alternar(LED_VERDE);
        }
        break;
    }
}

static void wk_deinit(void)
{
    gpio_poner(CAN_STB_PORT, CAN2_STB, 0);  // CAN2 vuelve al enlace
    PWR_CR3 &= ~(WKUP2 | WKUP3);
}

static void wk_estado_imprimir(void)
{
    static const char *const nombres[] = { "aviso (rojo 3 s)", "preparando", "preparando", "despierta", "preparando" };
    cprintf("  %s, wfi sin dormir %u\n", nombres[wk_estado], wk_no_durmio);
}

static const prueba_t p_wakeup = {
    .nombre = "wakeup", .archivo = "CAN_Esclavo_WakeUp",
    .descripcion = "Se duerme en Standby a los 3 s; la despierta CAN1 (WKUP3) o CAN2 (WKUP2)",
    .init = wk_init, .step = wk_step, .deinit = wk_deinit, .estado = wk_estado_imprimir,
};
