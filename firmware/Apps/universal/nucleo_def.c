// Núcleo del firmware Universal: tipos, declaraciones y puntos de extensión
// Se incluye antes del enlace y de las pruebas. La implementación está en nucleo.c, que cada
// firmware incluye después de definir su tabla de pruebas y sus funciones "extra"

#define PRUEBA_SOLO_LOCAL (1U << 0)  // usa FDCAN2: por CAN2 se pide como línea ("esclava correr ...")

typedef struct {
    const char *nombre;       // nombre corto para la consola
    const char *archivo;      // nombre del programa original (también se acepta en "correr")
    const char *descripcion;
    void (*init)(void);
    void (*step)(void);       // no bloqueante: lo llama el loop principal
    void (*deinit)(void);     // apaga lo propio de la prueba; DO1/DO3/DO4 y LEDs los apaga el núcleo
    void (*estado)(void);     // detalles para "estado" (puede ser NULL)
    volatile uint32_t *ok;    // contadores de tramas o lecturas (pueden ser NULL)
    volatile uint32_t *err;
    uint32_t flags;
} prueba_t;

// Pruebas de bring-up, con número fijo.
// Universal.c agrega las del plan de pruebas a partir de P_BASE_TOTAL
enum {
    P_NINGUNA, P_LEDS, P_DIGITAL, P_SALIDAS, P_LOOPBACK, P_ADC, P_PWM, P_ESP32,
    P_CAN_FD, P_CAN_MAESTRO, P_CAN_ESCLAVO, P_WAKEUP, P_LIN_MAESTRO, P_LIN_ESCLAVO,
    P_COMBO_MAESTRO, P_COMBO_ESCLAVO, P_BASE_TOTAL
};

// Tabla de pruebas del firmware (la carga nucleo_main)
static const prueba_t *const *pruebas;
static uint32_t pruebas_total;

// Implementadas en nucleo.c; las usan el enlace y algunas pruebas
static int prueba_correr(uint32_t n, int desde_can);
static void prueba_parar(void);
static uint32_t prueba_actual(void);
static const char *prueba_nombre(uint32_t n);
static uint32_t prueba_segundos(void);
static int prueba_contadores(uint32_t *ok, uint32_t *err);
static void par_local_ack(uint32_t prueba, uint32_t espera_ms, int wakeup);
static int texto_igual(const char *a, const char *b);
static int32_t texto_numero(const char *s);
static int texto_on_off(const char *s);
static uint32_t buscar_prueba(const char *s);
static void comando(char *l);  // ejecuta una línea de consola (también la que llega del maestro)
#define PAR_WAKEUP_ESPERA_MS 3500  // "par wakeup": la esclava muestra el rojo 3 s antes de dormirse

// Puntos de extensión: los define cada firmware (Universal los deja vacíos)
static int comando_extra(char **pal, uint32_t n);          // 1 si atendió el comando
static void ayuda_extra(void);                             // texto extra al final de "ayuda"
static int par_extra(const char *arg);                     // 1 si atendió "par <arg>"
static void arranque_extra(int retomo_prueba);             // al final del arranque
static void loop_extra(void);                              // en cada vuelta del loop
static uint32_t param_remoto(uint32_t clave, uint32_t valor);  // parámetro pedido por CAN2 (ERR_*)

// Parámetros que antes se cambiaban desde el Watch
static volatile uint32_t pwm_duty = 50;  // %, comando "duty"
static volatile uint32_t adc_canal;      // 0 = AN1 ... 3 = AN4, comando "canal"

// 1 cuando la placa arrancó porque la despertó un WKUP mientras corría CAN_Esclavo_WakeUp
static int wakeup_desperto;
#define WK_MARCA      0xEC320000U  // TAMP_BKP0R = WK_MARCA | número de prueba, antes de entrar en Standby
#define WK_MARCA_MASK 0xFFFF0000U
