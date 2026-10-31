# ECU32

**ECU universal para vehículos de 12 / 24 V basada en STM32G491**
Proyecto Integrador 3 · Ingeniería en Electrónica · Universidad ORT Uruguay
Santiago Acosta · Mateo Opertti

<p align="center">
  <a href="#pinout"><b>Pinout</b></a> ·
  <a href="hardware/schematic/ECU32_Schematic.pdf"><b>Esquemático (PDF)</b></a> ·
  <a href="hardware/pcb/ECU32_PCB_Layout.pdf"><b>PCB Layout (PDF)</b></a>
</p>

<p align="center">
  <img src="docs/images/renders/ecu32-3d.png" alt="Render 3D de ECU32" width="700">
</p>

ECU32 es una **ECU universal** para vehículos de 12 / 24 V: una placa de propósito general con CAN FD, LIN, entradas y salidas digitales y analógicas protegidas, y conectividad Wi-Fi/BLE. Se adapta a cada aplicación desde el firmware. Está pensada para el entorno automotriz, componentes AEC-Q100/Q200 y protecciónes contra transitorios ISO 16750-2 (cold crank y load dump).

---

## Contenido

- [Características](#características)
- [Diagrama de bloques](#diagrama-de-bloques)
- [Hardware](#hardware)
- [Gabinete](#gabinete)
- [Conectores](#conectores)
- [Pinout](#pinout)
- [Firmware](#firmware)
- [Estructura del repositorio](#estructura-del-repositorio)
- [Galería](#galería)
- [Estado del proyecto](#estado-del-proyecto)

---

## Características

| | |
|---|---|
| **MCU** | STM32G491VCT6: Cortex-M4F a 170 MHz, 256 KB de flash, LQFP100 |
| **Alimentación** | 12 V o 24 V nominal, con protección de polaridad inversa y sobretensión y TVS para load dump |
| **CAN FD** | 2 canales con terminación partida conmutable y wake-up por bus |
| **LIN** | 1 canal, con pull-up de master conmutable |
| **E/S digitales** | 4 DI y 4 DO protegidas, a través de un buffer con habilitación por grupo |
| **Entradas analógicas** | 4 canales con divisor, filtro RC, clamp y buffer op-amp |
| **Monitor de batería** | V_BAT medida por el ADC3 |
| **Conectividad** | Zócalo para ESP32-DEVKIT-V1 (Wi-Fi/BLE) conectado por UART |
| **Debug** | JTAG/SWD de 20 pines (ST-LINK, J-Link) |
| **PCB** | 2 capas, ~150 × 150 mm, fabricada en JLCPCB |

---

## Diagrama de bloques

```mermaid
flowchart LR
    BAT["J7 Batería 12 / 24 V"] --> PROT["Fusible + TVS<br/>LM74720-Q1<br/>ideal diode"]
    PROT --> BUCK["Buck AP64202<br/>5 V"]
    BUCK --> LDO["LDO TLE42744<br/>3,3 V"]
    PROT -. BAT_MON .-> MCU

    LDO --> MCU["STM32G491VCT6"]

    MCU <--> CAN["TCAN1046AV-Q1<br/>CAN FD ×2"]
    MCU <--> LIN["TLIN4029A-Q1<br/>LIN"]
    MCU <--> BUF["SN74LV244A<br/>4 DI / 4 DO"]
    AIN["4 entradas<br/>analógicas"] --> OPA["TLV9164<br/>buffer"] --> MCU
    MCU <--> ESP["ESP32 DevKit<br/>Wi-Fi / BLE"]
    JTAG["J2 JTAG/SWD"] <--> MCU

    CAN <--> J3["J3 CAN1 / CAN2 / LIN"]
    LIN <--> J3
    BUF <--> J1["J1 E/S digitales"]
```

---

## Hardware

| Bloque | Componente | Notas |
|---|---|---|
| Protección de entrada | F1, SMBJ58A + SMBJ28A, LM74720-Q1 + BUK7K15-80E | TVS en anti-serie e ideal diode con corte por OV (≈ 35 V) |
| Buck 5 V | AP64202SP-13 | |
| LDO 3,3 V | TLE42744GS-V33 | |
| MCU | STM32G491VCT6 | HSE de 16 MHz, LSE de 32,768 kHz |
| CAN FD | TCAN1046AVDYYRQ1 | VIO = 3,3 V; ESD con ESD2CAN36-Q1 |
| LIN | TLIN4029A-Q1 | Se maneja con el modo LIN del USART3 |
| E/S digitales | SN74LV244A | Por canal: R serie, zener, Schottky y TVS |
| Entradas analógicas | TLV9164 (seguidor) | Por canal: 5,6 k / 8,2 k, 10 nF y clamps BAS40 |
| Wi-Fi / BLE | ESP32-DEVKIT-V1 (zócalo) | Se alimenta desde 5 V a través de un Schottky |

El esquemático tiene 7 hojas, una por bloque: STM32, GPIOs, alimentación, CAN, LIN, entradas analógicas y ESP32. Está todo en [`hardware/schematic/ECU32_Schematic.pdf`](hardware/schematic/ECU32_Schematic.pdf). El layout (top y bottom) está en [`hardware/pcb/ECU32_PCB_Layout.pdf`](hardware/pcb/ECU32_PCB_Layout.pdf) y el modelo 3D en [`hardware/3d/ECU32.step`](hardware/3d/ECU32.step).

---

## Gabinete

La placa se protege con tapas de acrílico superior e inferior, sostenidas por separadores impresos en 3D con insertos roscados M2 que pasan por los agujeros de Ø 5 mm de la PCB. Evitan cortocircuitos accidentales al manipular la placa energizada. El modelo y las medidas están en [`hardware/enclosure/`](hardware/enclosure/).

---

## Conectores

| Ref. | Función | Pines |
|---|---|---|
| J7 | Alimentación (batería) | 2 |
| J3 | CAN1, CAN2 y LIN | 5 |
| J1 | E/S digitales | 8 |
| J4 | Entradas analógicas | 4 |
| J2 | JTAG/SWD (ARM de 20 pines) | 20 |
| J5 / J6 | Headers para el ESP32 DevKit | — |

**J1, pines de la bornera:**

| Pin J1 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| Señal | DO2 | DO1 | DO4 | DO3 | DI1 | DI2 | DI3 | DI4 |

**J3, CAN y LIN:**

| Pin J3 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| Señal | LIN | CAN2_L | CAN2_H | CAN1_H | CAN1_L |

**J4, entradas analógicas:**

| Pin J4 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| Señal | AN1 | AN2 | AN3 | AN4 |
| Rango | 0 – 5 V | 0 – 3,3 V | 0 – 12 V | 0 – 24 V |

**J7, alimentación:** pin 1 = VBAT y pin 2 = GND.

**Llaves de configuración:** SW2 y SW4 conectan la terminación de cada bus CAN, y SW3 conecta el pull-up de master LIN.

---

## Pinout

> **QR de la serigrafía.** La cara inferior de la placa tiene un [QR con el pinout](docs/images/qr-pinout.png). Ese QR se generó antes de verificar la netlist y tiene dos errores: dice DO2 = PC10 y DO3 = PE0 (es al revés), y agrupa las /OE mezclando DI y DO (en realidad 1/OE controla DO1–DO4 y 2/OE controla DI1–DI4). **Las tablas de esta sección son las correctas.**

### Niveles de tensión por conector

| Conector | Señales | Nivel de trabajo | Protección |
|---|---|---|---|
| J7 | VBAT, GND | 12 V o 24 V nominal | Fusible, TVS en anti-serie, corte por sobretensión (≈ 35 V) y protección de polaridad inversa |
| J1 | DI1 – DI4 | **3,3 V, 12 V o 24 V**, según el jumper (ver abajo) | 4,7 kΩ en serie, zener de 4,7 V, Schottky y TVS de 36 V |
| J1 | DO1 – DO4 | Salida lógica de 3,3 V a través de 4,7 kΩ (señal, no potencia) | Zener de 4,7 V, Schottky y TVS de 36 V |
| J4 | AN1 | 0 – 5 V | Divisor resistivo y filtro RC, clamps Schottky a 3,3 V y GND, y TVS de 36 V |
| J4 | AN2 | 0 – 3,3 V | Ídem |
| J4 | AN3 | 0 – 12 V | Ídem |
| J4 | AN4 | 0 – 24 V | Ídem |
| J3 | CAN1, CAN2 | Niveles CAN FD estándar | ESD2CAN36-Q1 por bus |
| J3 | LIN | Nivel de batería (bus LIN estándar) | Propia del TLIN4029A-Q1 |

### Selección de tensión de las entradas digitales (jumpers)

Cada entrada digital tiene, cerca de J1, una columna de jumpers con tres posiciones: **3V3**, **12V** y **24V**. La serigrafía de cada columna indica el pin del MCU:

| Columna (serigrafía) | C11 | D2 | E1 | E5 |
|---|---|---|---|---|
| Entrada | DI4 | DI3 | DI2 | DI1 |

| Jumper | Divisor (4,7 kΩ en serie +) | Tensión en el buffer a fondo de escala |
|---|---|---|
| **3V3** | sin resistencia a GND | 3,3 V (entrada directa) |
| **12V** | 1,5 kΩ a GND | 12 V → 2,90 V |
| **24V** | 680 Ω a GND | 24 V → 3,03 V |

Poné **un solo jumper por canal**. Las entradas están protegidas por el zener de 4,7 V aunque el jumper no coincida con la tensión aplicada, pero la lectura sólo es correcta si la posición del jumper corresponde a la señal.

### Entradas y salidas digitales (U5 SN74LV244A)

| Señal | Pin STM32 | Pin LQFP100 | | Señal | Pin STM32 | Pin LQFP100 |
|---|---|---|---|---|---|---|
| DI1 | PE5 | 4 | | DO1 | PB9 | 96 |
| DI2 | PE1 | 98 | | DO2 | PE0 | 97 |
| DI3 | PD2 | 84 | | DO3 | PC10 | 79 |
| DI4 | PC11 | 80 | | DO4 | PC12 | 81 |

| Habilitación (activa en bajo) | Pin STM32 | Controla |
|---|---|---|
| 1/OE | PE8 | DO1 – DO4 |
| 2/OE | PE10 | DI1 – DI4 |

Las dos /OE tienen pull-up a 3,3 V, así que al reset el buffer queda en alta impedancia.

### Entradas analógicas (ADC12)

| Señal | Pin STM32 | Canal ADC |
|---|---|---|
| AN1 | PB11 | ADC12_IN14 |
| AN2 | PA1 | ADC12_IN2 |
| AN3 | PC1 | ADC12_IN7 |
| AN4 | PA0 | ADC12_IN1 |

### CAN FD (U6 TCAN1046AV-Q1)

| Señal | CAN1 | CAN2 |
|---|---|---|
| TX | PA12 (FDCAN1_TX, AF9) | PB6 (FDCAN2_TX, AF9) |
| RX | PA11 (FDCAN1_RX, AF9) | PB5 (FDCAN2_RX, AF9) |
| STB (alto = standby) | PD14 | PD15 |
| Wake-up | PE6 (WKUP3) | PC13 (WKUP2) |

### LIN (U7 TLIN4029A-Q1)

| Señal | Pin STM32 | Función |
|---|---|---|
| TXD | PD8 | USART3_TX (AF7) |
| RXD | PD9 | USART3_RX (AF7) |
| EN | PD10 | GPIO; pull-down, así que arranca en sleep |

### ESP32

| Señal ESP32 | Pin STM32 | Función |
|---|---|---|
| RX2 | PD5 | USART2_TX (AF7) |
| TX2 | PD6 | USART2_RX (AF7) |
| D4 | PD4 | GPIO |
| D18 | PD3 | GPIO |
| EN | PE7 | GPIO open-drain |

### Internos, relojes y debug

| Función | Pin STM32 |
|---|---|
| Monitor de batería (V_BAT = 21,8 × V_PE9) | PE9 (ADC3_IN2) |
| LED1 (DS1) | PF9 |
| LED2 (DS2) | PF10 |
| HSE 16 MHz | PF0 / PF1 |
| LSE 32,768 kHz | PC14 / PC15 |
| JTAG / SWD | PA13, PA14, PA15, PB3, PB4 |
| BOOT0 | PB8 |
| Pull-down UCPD (deshabilita el *dead battery*) | PA9, PA10 |

---

## Firmware

Los programas de prueba del bring-up están en [`firmware/`](firmware/). Hay uno por periférico, escritos a nivel de registros (sin HAL), y el que se graba se elige desde [`Src/main.c`](firmware/Src/main.c).

| Programa | Qué prueba | Cómo se ve que funciona |
|---|---|---|
| `LEDs.c` | LED1 (PF9) y LED2 (PF10) | Parpadean cada 500 ms |
| `Digital.c` | Entrada DI4 (PC11) | Verde en alto, rojo en bajo |
| `Digital_Salidas.c` | Salida PC10, con el buffer U5 habilitado | Salida en alto y LED verde |
| `Digital_Loopback.c` | Lazo salida PC10 → entrada DI4 | El LED sigue el nivel leído |
| `ADC.c` | AN1 – AN4 (ADC1) | Tensión en mV en `an_mv[]` y LEDs por umbral |
| `CAN_FD.c` | FDCAN1 → FDCAN2, 500 kbit/s / 2 Mbit/s, 64 bytes | Verde alterna con cada trama correcta |
| `LIN_Maestro.c` / `LIN_Esclavo.c` | LIN 2.x a 19200 baud entre dos placas | Verde alterna con cada trama correcta |
| `PWM.c` | PWM de 500 kHz por PC10 (TIM8_CH1N) | Duty ajustable en vivo desde el debugger |

En el [README del firmware](firmware/README.md) están las conexiones de cada prueba y cómo compilar y grabar.

**Herramientas:** VS Code con la extensión STM32Cube (CMake + Ninja, GCC 14.3.1) y un ST-LINK conectado a J2. La placa no tiene USB propio; siempre se alimenta por J7.

### Notas de arranque

Puntos que hay que configurar sí o sí en el código de inicialización:

1. **Debug:** dejar el JTAG completo habilitado (PA15, PB3 y PB4 están cableados a J2).
2. **Relojes:** los programas de prueba usan el HSI16 por defecto. Para la aplicación final están el HSE de 16 MHz (que ya usa `CAN_FD.c`) y el LSE de 32,768 kHz; si el LSE no arranca, ajustar `LSEDRV`.
3. **Buffer de E/S:** PE8 y PE10 arrancan en alto. Después: fijar niveles seguros en las DO, bajar 1/OE y luego bajar 2/OE.
4. **CAN:** poner PD14 y PD15 en bajo para sacar los transceptores de standby.
5. **LIN:** poner PD10 en alto antes de transmitir. USART3 en modo LIN (`LINEN`).
6. **ESP32:** manejar PE7 como open-drain, porque el DevKit ya tiene su propio pull-up en EN.
7. **Wake-up por CAN:** WKUP3 (PE6) desde CAN1 y WKUP2 (PC13) desde CAN2.

---

## Estructura del repositorio

```
ECU32/
├── hardware/
│   ├── schematic/         Esquemático en PDF (7 hojas) + netlist PI_3.NET
│   ├── pcb/               Layout en PDF (top y bottom)
│   ├── 3d/                Modelo 3D de la placa (STEP)
│   ├── enclosure/         Separadores impresos en 3D para el gabinete (STL)
│   ├── fabrication/       Gerbers, NC Drill y Pick & Place (JLCPCB)      [pendiente]
│   └── bom/               BOM (CSV y Excel)
├── firmware/              Proyecto CMake (STM32Cube para VS Code)
│   ├── Apps/              Programas de prueba, uno por periférico
│   ├── Src/               main.c (selector de programa), startup y syscalls
│   └── cmake/             Toolchain y flags
├── docs/
│   ├── images/
│   │   ├── diagrams/      Diagrama de bloques inicial (PNG + fuente .cddx)
│   │   ├── renders/       Renders 3D
│   │   ├── pcb/           Capturas del layout
│   │   └── photos/        Fotos de la placa armada y de las pruebas
│   ├── manual/            Manual de usuario UM-ECU32                     [pendiente]
│   └── reports/           Anteproyecto (informe final pendiente)
└── README.md
```

Las carpetas marcadas como *pendiente* todavía sólo tienen un `README.md` que explica qué va adentro y cómo exportarlo desde Altium.

---

## Galería

| Placa armada (top) | Placa armada (bottom) |
|---|---|
| ![Placa armada, top](docs/images/photos/ecu32-assembled-top.jpg) | ![Placa armada, bottom](docs/images/photos/ecu32-assembled-bottom.jpg) |

![Render 3D de ECU32](docs/images/renders/ecu32-3d.png)

| Layout top | Layout bottom |
|---|---|
| ![Layout top](docs/images/pcb/ecu32-layout-top.png) | ![Layout bottom](docs/images/pcb/ecu32-layout-bottom.png) |

*Próximamente: fotos de las pruebas.*

---

## Estado del proyecto

| Etapa | Estado |
|---|---|
| Esquemático | Terminado |
| PCB layout | Terminado, fabricado por JLCPCB en octubre de 2026 |
| Componentes | Recibidos |
| Armado | Placa armada |
| Gabinete | Separadores impresos en 3D |
| Bring-up | En curso |
| Firmware | En curso: programas de prueba por periférico listos |
| Entrega final | 30/11/2026 |

---

## Equipo

Diseñado y desarrollado en conjunto por:

- **Santiago Acosta**
- **Mateo Opertti**

Proyecto Integrador 3, Universidad ORT Uruguay, 2026.
