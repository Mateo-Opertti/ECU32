// Selector de programa: dejá un solo #include sin comentar
// Pruebas.c: firmware para el plan de pruebas (todo lo de Universal + E/S, ADC, CAN, ESP32 e integral)
// Universal.c: firmware de bring-up, elige la prueba por consola o por CAN2 sin regrabar
// Las dos placas tienen que tener el mismo firmware
#include "../Apps/Pruebas.c"
//#include "../Apps/Universal.c"

// Programas sueltos anteriores (uno por prueba), por si hace falta volver a alguno
//#include "../Apps/legacy/LEDs.c"
//#include "../Apps/legacy/Digital.c"
//#include "../Apps/legacy/Digital_Salidas.c"
//#include "../Apps/legacy/Digital_Loopback.c"
//#include "../Apps/legacy/CAN_FD.c"
//#include "../Apps/legacy/CAN_Maestro.c"
//#include "../Apps/legacy/CAN_Esclavo.c"
//#include "../Apps/legacy/CAN_Esclavo_WakeUp.c"
//#include "../Apps/legacy/ADC.c"
//#include "../Apps/legacy/LIN_Maestro.c"
//#include "../Apps/legacy/LIN_Esclavo.c"
//#include "../Apps/legacy/ESP32_Serial.c"
//#include "../Apps/legacy/PWM.c"
