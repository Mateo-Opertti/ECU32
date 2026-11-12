// LEDs: LED1 (PF9, rojo) y LED2 (PF10, verde) parpadean juntos cada 500 ms

#define LEDS_BLINK_MS 500

static uint32_t leds_t0;
static int leds_prendidos;

static void leds_init(void)
{
    leds_prendidos = 1;  // como la app original: el primer cambio es al arrancar
    leds(1, 1);
    leds_t0 = ticks;
}

static void leds_step(void)
{
    if ((ticks - leds_t0) < LEDS_BLINK_MS) return;
    leds_t0 += LEDS_BLINK_MS;
    leds_prendidos = !leds_prendidos;
    leds(leds_prendidos, leds_prendidos);
}

static void leds_deinit(void) {}

static const prueba_t p_leds = {
    .nombre = "leds", .archivo = "LEDs",
    .descripcion = "LED1 (PF9) y LED2 (PF10) parpadean juntos cada 500 ms",
    .init = leds_init, .step = leds_step, .deinit = leds_deinit,
};
