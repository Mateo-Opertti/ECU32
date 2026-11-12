// Digital_Loopback: lazo salida -> entrada, con un cable de DO3 (PC10) a DI4 (PC11)
// Cada 500 ms cambia DO3, a los 5 ms lee DI4 y compara. Ya no hace falta usar breakpoints:
// cada lectura se informa por la consola y se cuenta como OK o error
// LEDs: siguen el nivel leído, como en la app original (bajo = rojo, alto = verde)

#define LOOP_PERIODO_MS  500
#define LOOP_LECTURA_MS  5    // tiempo para que la señal pase por U5, el cable y el divisor

static uint32_t loop_t;
static uint32_t loop_nivel;
static int loop_por_leer;
static volatile uint32_t loop_ok, loop_err;

static void loop_init(void)
{
    gpio_entrada(DI4_PORT, DI4, 2);  // pull-down, como la app original
    gpio_salida(DO3_PORT, DO3, 0);
    loop_nivel = 0;
    loop_t = ticks;
    loop_por_leer = 1;  // la primera lectura es con DO3 en bajo
    loop_ok = loop_err = 0;
}

static void loop_step(void)
{
    if (loop_por_leer && (ticks - loop_t) >= LOOP_LECTURA_MS) {
        loop_por_leer = 0;
        uint32_t leido = (uint32_t)gpio_leer(DI4_PORT, DI4);
        leds(!leido, leido);
        int ok = leido == loop_nivel;
        if (ok) loop_ok++;
        else loop_err++;
        cprintf("loopback: DO3 (PC10) = %u (el pin lee %u)  DI4 (PC11) = %u  %s\n",
                loop_nivel, (uint32_t)gpio_leer(DO3_PORT, DO3), leido, ok ? "OK" : "FALLA");
    }

    if ((ticks - loop_t) >= LOOP_PERIODO_MS) {
        loop_t = ticks;
        loop_nivel ^= 1U;
        gpio_poner(DO3_PORT, DO3, (int)loop_nivel);
        loop_por_leer = 1;
    }
}

static void loop_deinit(void)
{
    gpio_entrada(DI4_PORT, DI4, 0);
}

static const prueba_t p_loopback = {
    .nombre = "loopback", .archivo = "Digital_Loopback",
    .descripcion = "Lazo DO3 (PC10) -> DI4 (PC11) con un cable; informa cada lectura",
    .init = loop_init, .step = loop_step, .deinit = loop_deinit,
    .ok = &loop_ok, .err = &loop_err,
};
