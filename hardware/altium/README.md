# Esquemáticos de Altium (PI_3)

Fuentes editables de las 7 hojas del esquemático, en Altium Designer. Son una copia: el proyecto original se versiona en Altium 365.

| Archivo | Contenido |
|---|---|
| `STM32.SchDoc` | STM32G491VCT6, cristales, desacople, JTAG, BOOT0 y LEDs |
| `GPIOs.SchDoc` | E/S digitales protegidas (SN74LV244A) |
| `Alimentación.SchDoc` | Entrada de batería, TVS, LM74720-Q1, buck de 5 V, LDO de 3,3 V y BAT_MON |
| `Transceptor CAN.SchDoc` | CAN FD doble (TCAN1046AV-Q1), terminación y ESD |
| `Transceptor LIN.SchDoc` | LIN (TLIN4029A-Q1) |
| `Pin_Analogico.SchDoc` | Entradas analógicas (TLV9164) |
| `ESP32.SchDoc` | Zócalo del ESP32 DevKit |

El PDF de las 7 hojas está en [`../schematic/`](../schematic/). El layout (`.PcbDoc`) y el proyecto (`.PrjPcb`) no se publican.

## Cómo actualizar esta copia

Después de guardar en Altium, copiar los `.SchDoc` desde la carpeta del proyecto (`C:\Users\Public\Documents\Altium\PI_3`) a esta carpeta y hacer el commit.
