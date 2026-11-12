// Rol de control (MAESTRO / ESCLAVA) y parámetros guardados en la última página de flash
// G491xC: categoría 4, un solo banco, páginas de 2 KB -> página 127 = 0x0803F800..0x0803FFFF
// (reservada en stm32g491xc_flash.ld). Sobrevive al reset, al Standby y al corte de alimentación
// Formato: bitácora de doble-words { w0, ~w0 }. Cada cambio graba el siguiente doble-word libre;
// la página se borra recién cuando se llena y entonces se reescribe lo vigente
//   rol:       w0 = ROL_MAGIC | rol
//   parámetro: w0 = PARAM_MAGIC | clave << 16 | valor (16 bits), clave 0..15
// Sin ningún registro válido la placa arranca como ESCLAVA y sin parámetros

#define ROL_PAGINA    127U
#define ROL_DIR       0x0803F800UL
#define ROL_TAM       2048U
#define ROL_MAGIC     0x524F4C00U  // "ROL" + rol en el byte bajo
#define PARAM_MAGIC   0x50000000U  // 'P' + clave + valor
#define PARAM_CLAVES  16U
#define PARAM_VACIO   0xFFFFFFFFU

#define ROL_MAESTRO   1U
#define ROL_ESCLAVA   2U

static uint32_t rol_actual = ROL_ESCLAVA;
static int rol_grabado;                       // hay un registro de rol en la flash
static uint32_t params[PARAM_CLAVES];         // PARAM_VACIO = sin valor
static uint32_t rol_libre;                    // próximo doble-word libre (ROL_TAM / 8 = página llena)
volatile uint32_t rol_ecc_errores;            // doble-words ilegibles (por ejemplo, un corte mientras se grababa)

static const char *rol_nombre(uint32_t r)
{
    return r == ROL_MAESTRO ? "MAESTRO" : "ESCLAVA";
}

// Un doble-word a medio grabar da error de ECC doble, que dispara NMI. Se limpia y se sigue:
// el dato leído no pasa la verificación y ese registro se ignora
void NMI_Handler(void)
{
    if (FLASH_ECCR & ECCR_ECCD) {
        FLASH_ECCR = ECCR_ECCD;
        rol_ecc_errores++;
        return;
    }
    for (;;);
}

static void rol_leer(void)
{
    rol_actual = ROL_ESCLAVA;
    rol_grabado = 0;
    for (uint32_t k = 0; k < PARAM_CLAVES; k++) params[k] = PARAM_VACIO;
    rol_libre = ROL_TAM / 8;
    for (uint32_t i = 0; i < ROL_TAM / 8; i++) {
        uint32_t w0 = REG(ROL_DIR + 8 * i);
        uint32_t w1 = REG(ROL_DIR + 8 * i + 4);
        if (w0 == 0xFFFFFFFFU && w1 == 0xFFFFFFFFU) {
            rol_libre = i;
            break;
        }
        if (w1 != ~w0) continue;  // registro roto: se ignora
        uint32_t r = w0 & 0xFFU;
        if ((w0 & ~0xFFU) == ROL_MAGIC && (r == ROL_MAESTRO || r == ROL_ESCLAVA)) {
            rol_actual = r;
            rol_grabado = 1;
        } else if ((w0 & 0xFF000000U) == PARAM_MAGIC) {
            uint32_t clave = (w0 >> 16) & 0xFFU;
            if (clave < PARAM_CLAVES) params[clave] = w0 & 0xFFFFU;
        }
    }
}

static void flash_vaciar_cache(void)
{
    if (FLASH_ACR & ACR_DCEN) {
        FLASH_ACR &= ~ACR_DCEN;
        FLASH_ACR |= ACR_DCRST;
        FLASH_ACR &= ~ACR_DCRST;
        FLASH_ACR |= ACR_DCEN;
    }
}

// Mientras se borra o se graba, el CPU se frena al leer la flash (un solo banco):
// unos 22 ms al borrar la página y unos 0,1 ms por doble-word
static int flash_esperar(void)
{
    while (FLASH_SR & SR_BSY);
    uint32_t err = FLASH_SR & SR_ERRORES;
    FLASH_SR = SR_ERRORES;
    return err == 0;
}

static int flash_grabar_dw(uint32_t w0)
{
    uint32_t dir = ROL_DIR + 8 * rol_libre;
    FLASH_CR |= FCR_PG;
    __asm volatile ("cpsid i");  // las dos palabras del doble-word, seguidas
    REG(dir) = w0;
    REG(dir + 4) = ~w0;
    __asm volatile ("cpsie i");
    int ok = flash_esperar();
    FLASH_CR &= ~FCR_PG;
    rol_libre++;
    return ok;
}

// Graba un registro nuevo. Si la página está llena, la borra y reescribe primero lo vigente
static int flash_registrar(uint32_t w0)
{
    int ok = 1;

    if (FLASH_CR & FCR_LOCK) {
        FLASH_KEYR = FLASH_KEY1;
        FLASH_KEYR = FLASH_KEY2;
    }
    flash_esperar();

    if (rol_libre >= ROL_TAM / 8) {
        FLASH_CR = (FLASH_CR & ~(0xFFU << FCR_PNB_SH)) | FCR_PER | (ROL_PAGINA << FCR_PNB_SH);
        FLASH_CR |= FCR_STRT;
        ok = flash_esperar();
        FLASH_CR &= ~(FCR_PER | (0xFFU << FCR_PNB_SH));
        rol_libre = 0;
        if (ok && rol_grabado) ok = flash_grabar_dw(ROL_MAGIC | rol_actual);
        for (uint32_t k = 0; ok && k < PARAM_CLAVES; k++) {
            if (params[k] != PARAM_VACIO) ok = flash_grabar_dw(PARAM_MAGIC | (k << 16) | params[k]);
        }
    }

    if (ok) ok = flash_grabar_dw(w0);

    FLASH_CR |= FCR_LOCK;
    flash_vaciar_cache();
    rol_leer();  // relee lo grabado: queda en RAM lo que de verdad quedó en la flash
    return ok;
}

static int rol_guardar(uint32_t r)
{
    return flash_registrar(ROL_MAGIC | r) && rol_actual == r;
}

// Parámetros: valor de 0 a 65535, o PARAM_VACIO si nunca se grabó
__attribute__((unused)) static uint32_t param_leer(uint32_t clave)
{
    return clave < PARAM_CLAVES ? params[clave] : PARAM_VACIO;
}

__attribute__((unused)) static int param_guardar(uint32_t clave, uint32_t valor)
{
    if (clave >= PARAM_CLAVES || valor > 0xFFFFU) return 0;
    if (params[clave] == valor) return 1;  // ya estaba: no gasta flash
    return flash_registrar(PARAM_MAGIC | (clave << 16) | valor) && params[clave] == valor;
}
