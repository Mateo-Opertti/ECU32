# Esquemático

| Archivo | Contenido |
|---|---|
| `ECU32_Schematic.pdf` | Las 7 hojas del esquemático (A4) |
| `PI_3.NET` | Netlist exportada desde Altium: **fuente de verdad del pinout** |

## Hojas

| Página | Hoja | Contenido |
|---|---|---|
| 1 | STM32 | STM32G491VCT6, cristales, desacople, JTAG, BOOT0 y LEDs |
| 2 | GPIOs | E/S digitales protegidas (SN74LV244A) |
| 3 | Alimentación | Entrada de batería, TVS, LM74720-Q1, buck de 5 V, LDO de 3,3 V y BAT_MON |
| 4 | Transceptor CAN | CAN FD doble (TCAN1046AV-Q1), terminación y ESD |
| 5 | Transceptor LIN | LIN (TLIN4029A-Q1) |
| 6 | Pin_Analogico | Entradas analógicas (TLV9164) |
| 7 | ESP32 | Zócalo del ESP32 DevKit |

## Cómo regenerarlos

- **PDF:** desde Altium, imprimir todas las hojas a PDF (Smart PDF o *Print to PDF*).
- **Netlist:** Design → Netlist For Project → Protel. El archivo sale en `Project Outputs for PI_3/PI_3.NET`.
