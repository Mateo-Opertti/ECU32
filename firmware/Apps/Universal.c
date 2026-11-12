// Firmware Universal de ECU32: un solo programa, grabado igual en las dos placas, que corre
// cualquiera de las pruebas de bring-up y se maneja por consola serie o desde la otra placa por CAN2
//
// Consola: USART1 a 115200 8N1 por J1: TX = DO2 (J1-1, PE0) y RX = DI2 (J1-6, PE1), a través de U5
// Enlace de control: FDCAN2 (CAN2 de J3). El MAESTRO le manda comandos a la ESCLAVA
// Rol: se guarda en la última página de flash (rol.c). Sin rol grabado, la placa es ESCLAVA
//
// Cada prueba es un módulo con init(), step() no bloqueante y deinit(). El loop principal atiende
// la consola, el enlace CAN2 y la prueba que esté corriendo, así que nada se queda esperando.
// La consola, los comandos y el loop están en universal/nucleo.c, que comparte con Pruebas.c
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
#include "universal/nucleo_def.c"
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

static const prueba_t *const tabla[P_BASE_TOTAL] = {
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
};

// Universal no agrega nada al núcleo
static int comando_extra(char **pal, uint32_t n) { (void)pal; (void)n; return 0; }
static void ayuda_extra(void) {}
static int par_extra(const char *arg) { (void)arg; return 0; }
static void arranque_extra(int retomo_prueba) { (void)retomo_prueba; }
static void loop_extra(void) {}
static uint32_t param_remoto(uint32_t clave, uint32_t valor) { (void)clave; (void)valor; return ERR_COMANDO; }

#include "universal/nucleo.c"

int main(void)
{
    nucleo_main(tabla, P_BASE_TOTAL, "Universal");
    return 0;
}
