# Firmware

Programas de prueba para el bring-up de ECU32 (STM32G491VCT6).

El proyecto es CMake + Ninja y se usa con la extensión **STM32Cube para VS Code**. No usa HAL: cada programa configura los periféricos escribiendo directo en los registros (RM0440) y corre con el reloj por defecto, HSI16 = 16 MHz.

## Cómo elegir el programa

Cada archivo de [`Apps/`](Apps/) es un programa completo con su propio `main()`. [`Src/main.c`](Src/main.c) sólo incluye uno de ellos. Para cambiar de programa, descomentá el `#include` que quieras y dejá los demás comentados (uno solo a la vez):

```c
//#include "../Apps/LEDs.c"
//#include "../Apps/CAN_FD.c"
#include "../Apps/PWM.c"
```

## Programas

| Archivo | Qué prueba | Cómo se ve que funciona |
|---|---|---|
| [`LEDs.c`](Apps/LEDs.c) | LED1 (PF9, rojo) y LED2 (PF10, verde) | Los dos LEDs parpadean juntos cada 500 ms |
| [`Digital.c`](Apps/Digital.c) | Entrada digital DI4 (PC11) | Alto: LED verde. Bajo: LED rojo |
| [`Digital_Salidas.c`](Apps/Digital_Salidas.c) | Salida digital en PC10, con 1/OE y 2/OE (PE8, PE10) habilitadas | La salida queda en alto y el LED verde prendido |
| [`Digital_Loopback.c`](Apps/Digital_Loopback.c) | Lazo salida → entrada: cable de PC10 a DI4 (PC11) | El LED sigue el nivel leído; se avanza con breakpoints |
| [`ADC.c`](Apps/ADC.c) | AN1 – AN4 con el ADC1, cada 100 ms | Tensión en el pin del ADC, en mV, en `an_mv[]` (Watch). LEDs según la entrada `CANAL_LED`: < 1,1 V rojo, 1,1 – 2,2 V los dos, > 2,2 V verde |
| [`CAN_FD.c`](Apps/CAN_FD.c) | FDCAN1 transmite y FDCAN2 recibe: 500 kbit/s nominal, 2 Mbit/s en datos (BRS), 64 bytes | El verde alterna con cada trama correcta. El rojo indica error o 500 ms sin recibir |
| [`LIN_Maestro.c`](Apps/LIN_Maestro.c) / [`LIN_Esclavo.c`](Apps/LIN_Esclavo.c) | LIN 2.x a 19200 baud entre dos placas, USART3 en modo LIN, checksum mejorado. ID 0x10: maestro → esclavo; ID 0x11: esclavo → maestro | El verde alterna con cada trama correcta. El rojo indica error o 500 ms sin una buena |
| [`CAN_Maestro.c`](Apps/CAN_Maestro.c) / [`CAN_Esclavo.c`](Apps/CAN_Esclavo.c) | CAN FD entre dos placas por FDCAN1: 500 kbit/s nominal, 2 Mbit/s en datos (BRS), 64 bytes. ID 0x100: maestro → esclavo; ID 0x101: esclavo → maestro | Esclavo: juego de luces que avanza con cada trama correcta. Maestro: el verde alterna con cada respuesta correcta. Rojo fijo: error o 500 ms sin una trama buena |
| [`CAN_Esclavo_WakeUp.c`](Apps/CAN_Esclavo_WakeUp.c) | Despertar desde Standby por actividad en CAN1, a través de WKUP3 (PE6) | Rojo 3 s y se duerme con los LEDs apagados. Cuando llega actividad por CAN1, el verde alterna cada 250 ms |
| [`ESP32_Serial.c`](Apps/ESP32_Serial.c) / [`ESP32_LED_Serial.ino`](ESP32/ESP32_LED_Serial/ESP32_LED_Serial.ino) | Enlace con la ESP32 del zócalo: USART2 a 115200 baud (PD5 → GPIO16). El STM32 manda `ON` y `OFF`, y la ESP32 maneja su LED interno | El LED interno de la ESP32 parpadea igual que el verde del STM32, cada 500 ms |
| [`PWM.c`](Apps/PWM.c) | PWM de 500 kHz por PC10 con el TIM8 (CH1N, AF4) | El duty se cambia en vivo modificando `duty_pct` (0 – 100) en el Watch |

**Conexiones para las pruebas:**

- **CAN FD:** unir CANH1 con CANH2 y CANL1 con CANL2 en J3, con una terminación de 120 Ω (SW2 o SW4).
- **CAN entre dos placas:** unir CANH1, CANL1 y GND entre las placas, con SW2 en ON en las dos. Una placa lleva `CAN_Maestro.c` y la otra `CAN_Esclavo.c`.
- **Wake-up por CAN:** misma conexión, con `CAN_Esclavo_WakeUp.c` en una placa y `CAN_Maestro.c` en la otra. Grabar, cortar el debug, apretar reset y prender la placa maestra recién cuando se apague el rojo. Para repetir la prueba, apagar la maestra y apretar reset.
- **ESP32:** módulo DevKit V1 en J5/J6, sin cables extra. El sketch se graba por USB desde Arduino, con la placa **DOIT ESP32 DEVKIT V1**.
- **LIN:** dos placas, una con `LIN_Maestro.c` y otra con `LIN_Esclavo.c`, unidas por el pin LIN de J3 y GND. En la placa maestra, SW3 cerrado (pull-up de master).
- **ADC:** cada entrada de J4 tiene su propio rango: AN1 de 0 a 5 V, AN2 de 0 a 3,3 V, AN3 de 0 a 12 V y AN4 de 0 a 24 V.

## Compilar y grabar

1. Abrir la carpeta `firmware/` en VS Code con la extensión STM32Cube instalada. Las versiones del toolchain (GCC 14.3.1, CMake, Ninja, ST-LINK GDB server) están fijadas en [`.settings/`](.settings/) y la extensión las descarga sola.
2. Elegir el preset **Debug** y compilar. La salida queda en `build/Debug/`, que no se versiona.
3. Conectar el ST-LINK a J2 y lanzar **STM32Cube: STM32 Launch ST-Link GDB Server** (F5). Compila, graba y frena en `main`.

## Estructura

```
firmware/
├── Apps/                  Un programa de prueba por archivo
├── ESP32/                 Sketch de Arduino para la ESP32 del zócalo
├── Src/
│   ├── main.c             Selector de programa
│   ├── startup_stm32g491xx.S
│   ├── syscall.c, sysmem.c
├── Inc/
├── cmake/                 Toolchain y flags (generados por STM32Cube)
├── CMakeLists.txt
├── CMakePresets.json      Presets Debug y Release
├── stm32g491xc_flash.ld   Script del linker
├── .settings/             Versiones del toolchain de STM32Cube
└── .vscode/               Configuración de debug e IntelliSense
```

Las notas de arranque (orden de habilitación de /OE, STB de CAN, EN de LIN, etc.) están en el [README principal](../README.md#firmware-notas-de-arranque).
