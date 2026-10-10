# ECU32

**Proyecto Integrador 3** · Acosta – Opertti · Universidad ORT Uruguay
**MCU:** STM32G491

## Pinout

### Entradas digitales

| Señal | Pin STM32 |
|-------|-----------|
| DI1   | PE5       |
| DI2   | PE1       |
| DI3   | PD2       |
| DI4   | PC11      |

### Salidas digitales

| Señal | Pin STM32 |
|-------|-----------|
| DO1   | PB9       |
| DO2   | PC10      |
| DO3   | PE0       |
| DO4   | PC12      |

### Habilitación del buffer (activa en bajo)

| Señal | Pin STM32 | Canales que habilita   |
|-------|-----------|------------------------|
| OE1   | PE8       | DI2, DI4, DO2, DO4     |
| OE2   | PE10      | DI1, DI3, DO1, DO3     |

### Entradas analógicas

| Señal | Pin STM32 |
|-------|-----------|
| AN1   | PB11      |
| AN2   | PA1       |
| AN3   | PC1       |
| AN4   | PA0       |

### CAN

| Señal | CAN 1 | CAN 2 |
|-------|-------|-------|
| TX    | PA12  | PB6   |
| RX    | PA11  | PB5   |
| STB   | PD14  | PD15  |
| WAKE  | PE6   | PC13  |

### LIN

| Señal | Pin STM32 |
|-------|-----------|
| TX    | PD8       |
| RX    | PD9       |
| EN    | PD10      |

### ESP32

Señal del ESP32 → pin del STM32.

| Señal ESP32 | Pin STM32 |
|-------------|-----------|
| RX2         | PD5       |
| TX2         | PD6       |
| D4          | PD4       |
| D18         | PD3       |
| EN          | PE7       |

### Interno

| Función             | Pin STM32 |
|---------------------|-----------|
| Monitor de batería  | PE9       |
| LED1                | PF9       |
| LED2                | PF10      |
