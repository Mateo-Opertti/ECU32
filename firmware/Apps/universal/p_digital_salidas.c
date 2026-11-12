// Digital_Salidas: DO3 (PC10) en alto y el LED verde prendido mientras corre
// Por la consola, una vez por segundo: lo que se le ordenó a DO3 y lo que lee el pin PC10.
// Si el pin no sube, la salida está en corto o con demasiada carga (se cuenta como error).
// Es el nivel en el pin del MCU: lo que sale por J1 después de U5 hay que medirlo con el tester
// U5 ya está habilitado desde el arranque (1/OE = PE8 y 2/OE = PE10 en bajo)

#define SAL_IMPRIMIR_MS 1000U

static uint32_t sal_t;
static volatile uint32_t sal_ok, sal_err;

static void salidas_init(void)
{
    gpio_salida(DO3_PORT, DO3, 1);
    leds(0, 1);
    sal_ok = sal_err = 0;
    sal_t = ticks;  // primer chequeo al segundo, con el pin ya estable
}

static void salidas_step(void)
{
    if ((ticks - sal_t) < SAL_IMPRIMIR_MS) return;
    sal_t = ticks;

    uint32_t pin = (uint32_t)gpio_leer(DO3_PORT, DO3);
    if (pin) sal_ok++;
    else sal_err++;
    cprintf("salidas: DO3 (PC10) ordenado = 1, el pin lee %u  %s\n",
            pin, pin ? "OK" : "FALLA (el pin no sube: corto o mucha carga)");
}

static void salidas_deinit(void) {}  // DO3 vuelve a bajo con las salidas seguras

static const prueba_t p_salidas = {
    .nombre = "salidas", .archivo = "Digital_Salidas",
    .descripcion = "Salida DO3 (PC10) en alto y LED verde; informa el nivel del pin",
    .init = salidas_init, .step = salidas_step, .deinit = salidas_deinit,
    .ok = &sal_ok, .err = &sal_err,
};
