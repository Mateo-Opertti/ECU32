// Digital: entrada DI4 (PC11) -> LEDs. Alto: verde (PF10). Bajo: rojo (PF9)
// Por la consola: cada vez que cambia DI4 y una vez por segundo (si "resumen on"), el nivel de DI4 y de las otras
// entradas (DI1 = PE5, DI3 = PD2). DI2 no se muestra: es el RX de la consola
// U5 ya está habilitado desde el arranque (2/OE = PE10 en bajo)

#define DIG_IMPRIMIR_MS  1000U  // reporte periódico
#define DIG_MIN_MS       100U   // como mucho un reporte cada 100 ms si la entrada rebota

static uint32_t dig_t;
static uint32_t dig_ultimo;  // último nivel informado (2 = todavía ninguno)
static volatile uint32_t dig_cambios;

static void digital_imprimir(uint32_t alto, int cambio)
{
    cprintf("digital: DI4 (PC11) = %u -> LED %s   [DI1 (PE5) = %u  DI3 (PD2) = %u]%s\n",
            alto, alto ? "verde" : "rojo",
            (uint32_t)gpio_leer(DI1_PORT, DI1), (uint32_t)gpio_leer(DI3_PORT, DI3),
            cambio ? "  (cambio)" : "");
}

static void digital_init(void)
{
    gpio_entrada(DI4_PORT, DI4, 2);  // pull-down, como la app original
    dig_ultimo = 2;
    dig_cambios = 0;
    dig_t = ticks - DIG_IMPRIMIR_MS;  // primer reporte enseguida
}

static void digital_step(void)
{
    uint32_t alto = (uint32_t)gpio_leer(DI4_PORT, DI4);
    leds(!alto, alto);

    int cambio = alto != dig_ultimo;
    uint32_t dt = ticks - dig_t;
    if ((cambio && dt >= DIG_MIN_MS) || (resumen_activo && dt >= DIG_IMPRIMIR_MS)) {
        if (cambio && dig_ultimo != 2) dig_cambios++;
        digital_imprimir(alto, cambio && dig_ultimo != 2);
        dig_ultimo = alto;
        dig_t = ticks;
    }
}

static void digital_deinit(void)
{
    gpio_entrada(DI4_PORT, DI4, 0);
}

static void digital_estado(void)
{
    cprintf("  DI4 (PC11) = %u, cambios %u\n", (uint32_t)gpio_leer(DI4_PORT, DI4), dig_cambios);
}

static const prueba_t p_digital = {
    .nombre = "digital", .archivo = "Digital",
    .descripcion = "Entrada DI4 (PC11): alto = verde, bajo = rojo; informa DI1/DI3/DI4",
    .init = digital_init, .step = digital_step, .deinit = digital_deinit, .estado = digital_estado,
};
