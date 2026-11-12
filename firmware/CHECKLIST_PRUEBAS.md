# Checklist del plan de pruebas

Orden de ejecución y forma de correr cada prueba de `plan_pruebas_ecu.pdf` con el firmware **Pruebas** ([`Apps/Pruebas.c`](Apps/Pruebas.c)). Las casillas se tildan en GitHub al terminar cada prueba con resultado conforme. Los valores medidos van en la hoja de registro del plan (anexo B).

- **Placa A:** rol MAESTRO, con el adaptador CP2102 de la consola.
- **Placa B:** rol ESCLAVA. Se maneja desde A por CAN2. Las pruebas **solo locales** (`banco_can`, `integral`) se corren con el CP2102 conectado a la placa que se prueba.
- En la consola, `plan` muestra un resumen de esta lista y `estado` da los contadores de la prueba que está corriendo.

Comandos y conexiones en detalle: [README del firmware](README.md#firmware-pruebas-plan-de-pruebas).

---

## 0. Preparación

- [ ] Firmware Pruebas grabado en A y en B (`Src/main.c` con `#include "../Apps/Pruebas.c"`)
- [ ] Sketch [`ESP32_Pruebas`](ESP32/ESP32_Pruebas/ESP32_Pruebas.ino) grabado en la ESP32 (queda **desenchufada** hasta la sección 9)
- [ ] Consola: CP2102 TX → J1-6 (DI2), RX → J1-1 (DO2), GND común, jumper de DI2 en 3V3, terminal a 115200 8N1
- [ ] `rol maestro` en A (B queda ESCLAVA)
- [ ] CAN2 entre placas (J3-3 con J3-3, J3-2 con J3-2, GND) y `esclava estado` contesta ACK
- [ ] Divisores de cada canal cargados en A y en B: `divisor 1 <Rs> <Rg>` … `divisor 4 <Rs> <Rg>` (verificar con `factor`)

## 1. Verificaciones documentales y por simulación

**DOC-01 · Calificación AEC-Q de la BOM** (CE-25)
- Sin firmware. Columna de calificación en la BOM y justificación de cada excepción.
- [ ] Hecho

**DOC-02 · Simulaciones de transitorios, ESD e inyección** (CE-03, CE-13, CE-10)
- Sin firmware. Todo por debajo de su máximo absoluto; inyección ≤ 5 mA por pin y ≤ 25 mA en total.
- [ ] Hecho

## 2. Inspección sin energía

**INS-01 · Inspección visual y cortocircuitos en rieles** (CE-14)
- Sin firmware. Soldaduras, polaridades, fusible, VBAT unido a VDD, resistencia a GND de VIN, VINP, 5 V, 3V3 y VDDA.
- [ ] Placa A
- [ ] Placa B

**INS-02 · Cantidad de canales y testpoints** (CE-15, CE-19)
- Sin firmware. 4 AN, 4 DI, 4 DO, 2 CAN, 1 LIN; testpoint por cada E/S y por cada riel.
- [ ] Placa A
- [ ] Placa B

## 3. Alimentación y programación

**ALI-01 · Primera energización** (CE-06)
- Límite de 100 mA, subir VIN de 0 a 12 V despacio. 3V3 entre 2,97 V y 3,63 V.
- [ ] Placa A
- [ ] Placa B

**ALI-02 · Riel de 3,3 V en rango de entrada y carga** (CE-06)
- VIN a 8, 12, 24 y 28 V; carga en 3V3 hasta 1,5 veces el consumo. 3V3 entre 2,97 V y 3,63 V.
- [ ] Placa A
- [ ] Placa B

**ALI-03 · Programación y depuración por SWD** (CE-20)
- ST-LINK en J2: leer el ID, grabar Pruebas y depurar con un breakpoint en `main`.
- [ ] Placa A
- [ ] Placa B

## 4. Entradas y salidas digitales

**DIG-01 · Salidas: funcionamiento y 4 mA** (CE-16, CE-24)
- Cableado: carga de 825 Ω de la salida a GND (VOH) o a 3,3 V (VOL).
- Comandos: `do 1 1`, `do 1 0`, `do 3 1`, `do 3 0`, `do 4 1`, `do 4 0`. DO2: VOH con la consola quieta (en reposo está en alto) y `do 2 0 30` para VOL.
- Criterio: cambia solo la salida pedida; VOH ≥ 2,4 V y VOL ≤ 0,4 V a 4 mA (multímetro).
- [ ] Placa A
- [ ] Placa B

**DIG-02 · Entradas en las tres posiciones del divisor** (CE-24)
- Cableado: fuente en cada DI, jumper en 3V3, 12V y 24V. Para DI2, desconectar el TX del CP2102.
- Comandos: `correr es` (avisa cada cambio de entrada) y `di`. Con una rampa lenta, el aviso marca el umbral.
- Criterio: lectura correcta en los 12 casos; solo cambia el canal estimulado.
- [ ] Placa A
- [ ] Placa B

**DIG-03 · Conmutación a 100 kHz** (CE-17)
- Cableado: carga de 4 mA en la salida; cuadrada de 100 kHz en la entrada (con etapa de potencia para 12 y 24 V).
- Comandos: `pwm 1 100000 50`, `pwm 3 100000 50`, `pwm 4 100000 50`; `frec on` mide las entradas (de a una por vez).
- Criterio: tr y tf ≤ 1 µs en salidas y entradas (osciloscopio); frecuencia medida = 100000 Hz.
- [ ] Placa A
- [ ] Placa B

## 5. Entradas analógicas

**AI-01 · Impedancia de entrada** (CE-18)
- Sin firmware especial. Fondo de escala a través de una R serie conocida, con y sin alimentación. Z ≥ 100 kΩ.
- [ ] Placa A
- [ ] Placa B

**AI-02 · Lectura de los cuatro canales** (CE-24)
- Cableado: fuente y multímetro en la entrada.
- Comandos: `correr ai`; en cada canal aplicar 0, 50 y 100 % del fondo de escala y escribir `ref <canal> <mV del multímetro>`. Repetir con `adc_conv 2` (ADC2) y volver con `adc_conv 1`.
- Criterio: error ≤ 1 % FS (PASA en `ref`); solo cambia el canal estimulado.
- [ ] Placa A con ADC1
- [ ] Placa A con ADC2
- [ ] Placa B con ADC1
- [ ] Placa B con ADC2

## 6. Comunicaciones

**COM-01 · Terminación CAN seleccionable** (CE-22)
- Sin energía: CANH–CANL de cada canal con el switch cerrado (120 Ω ± 5 %) y abierto (decenas de kΩ).
- [ ] Placa A
- [ ] Placa B

**COM-02 · CAN y CAN FD entre CAN1 y CAN2** (CE-21)
- Cableado: CANH1–CANH2 y CANL1–CANL2 en J3, las dos terminaciones activas, sin CAN2 hacia la otra placa.
- Comandos: `can_cantidad 100000`; `can_modo clasico` y `correr banco_can`; después `can_modo fd` y `correr banco_can`.
- Criterio: informe final con 0 perdidas, 0 errores y TEC = REC = 0 (PASA). Vdiff dominante entre 1,5 V y 3,0 V y recesivo < 0,5 V (osciloscopio).
- [ ] Placa A, CAN clásico
- [ ] Placa A, CAN FD
- [ ] Placa B, CAN clásico
- [ ] Placa B, CAN FD

**COM-03 · LIN como commander y responder** (CE-23)
- Cableado: LIN (J3-1) y GND entre placas; SW3 cerrado en la placa commander.
- Comandos: `par lin` (A commander) hasta unas 10 000 tramas; cambiar SW3 y `par lin_inv` (B commander). Ver los errores por tipo con `estado` y `esclava estado`.
- Criterio: 0 errores de checksum, sync o respuesta ausente; break, sync, PID y checksum correctos en el decodificador.
- [ ] A commander, B responder
- [ ] B commander, A responder

## 7. Integración

**INT-01 · Firmware integral en el rango de alimentación** (CE-24, CE-01)
- Cableado: puente CAN1–CAN2 en J3, LIN a la otra placa, cargas de 4 mA en las salidas. CAN2 hacia la otra placa puede quedar.
- Comandos: `integral 10` (o `integral 10 todas` para PWM también en DO2) a 8, 12, 24 y 28 V.
- Arranques en frío: `autoarranque integral` en la placa probada y `autoarranque lin_esclavo` en la otra; 5 arranques a 8 V y 5 a 28 V. Al terminar, `autoarranque no` en las dos.
- Criterio: informe final PASA (CAN, LIN y ADC sin errores) en cada tensión; arranque correcto en todos los casos.
- [ ] Placa A a 8, 12, 24 y 28 V
- [ ] Placa A, arranques en frío
- [ ] Placa B a 8, 12, 24 y 28 V
- [ ] Placa B, arranques en frío

**INT-02 · Consumo propio** (CE-07)
- Comandos: `can_tasa max` e `integral` (carga de bus alta). Medir la corriente de entrada a 12 V, sin ESP32.
- Criterio: ≤ 300 mA.
- [ ] Placa A
- [ ] Placa B

## 8. Nivel extendido

**EXT-01 · Segunda placa verificada**
- La placa B completó INS a INT. Comparar corriente a 12 V, 3V3, VOH a 4 mA, ADC a 50 % FS y umbrales de entrada.
- [ ] Hecho

**EXT-02 · CAN FD entre placas con arbitraje** (CE-21)
- Cableado: CAN1–CAN1 y CAN2–CAN2 entre placas, terminación en las puntas de cada bus.
- Comandos: `can_modo fd`, `can_tasa max`, `can_cantidad 100000`, `par canx`. Las dos placas transmiten a la vez: 0x120 (A) y 0x121 (B) por los dos buses.
- Criterio: los dos informes (A y `[esclava]`) con 0 perdidas y 0 errores; en el decodificador gana 0x120 y la 0x121 se retransmite.
- [ ] Hecho

**EXT-03 · Gateway entre buses** (CE-21)
- Cableado: igual que EXT-02.
- Comandos: `par gateway` (A reenvía CAN1 clásico a CAN2 FD; B manda y mide). Para carga alta, `can_tasa max`.
- Criterio: informe de `gw_fuente` con 0 pérdidas; registrar la latencia (mínima, promedio y máxima).
- [ ] Hecho

**EXT-04 · LIN maestro-esclavo de larga duración** (CE-23)
- Cableado: igual que COM-03.
- Comandos: `par lin` 30 min y `par lin_inv` 30 min, a VSUP = 12 V y a 24 V.
- Criterio: 0 errores de checksum, sync o respuesta ausente (`estado`).
- [ ] A commander a 12 V
- [ ] A commander a 24 V
- [ ] B commander a 12 V
- [ ] B commander a 24 V

**EXT-05 · Serigrafía**
- Sin firmware. Cada terminal y switch identificado; una persona ajena conecta la placa solo con la serigrafía y el manual.
- [ ] Hecho

**EXT-06 · Gabinete de acrílico**
- Accesos a borneras, switches, SWD, testpoints y USB de la ESP32; sin conductores expuestos.
- Comandos: `integral 60` con el gabinete cerrado.
- Criterio: informe PASA, sin cortes por temperatura.
- [ ] Hecho

## 9. Conector ESP32 (enchufar la ESP32 recién ahora)

**ESP-01 · Alimentación del módulo** (CE-06)
- Con el header vacío, medir 5 V, 3V3 y GND. Enchufar la ESP32 y escribir `esp wifi on`.
- Criterio: 3V3 del STM32 ± 10 % durante las ráfagas (osciloscopio) y sin resets. Un reset se nota en la consola por el mensaje de arranque.
- [ ] Placa A
- [ ] Placa B

**ESP-02 · UART y líneas digitales STM32 ↔ ESP32** (CE-24)
- Comandos: `esp ping`, `esp eco`, `esp enviar`, `esp recibir` (1 MB con CRC); `esp baud 921600` y repetir `esp eco`, `esp enviar` y `esp recibir`; `esp lineas`.
- Criterio: PASA en cada uno (0 errores de CRC a 921 600; las 4 líneas correctas). tr en los dos extremos con el osciloscopio.
- [ ] Placa A
- [ ] Placa B

**ESP-03 · Consumo con la ESP32** (CE-07)
- Comandos: `esp wifi on` + `integral`. Medir la corriente a 12 V. Informativo, junto al valor de INT-02.
- [ ] Placa A
- [ ] Placa B

**ESP-04 · ESP32 por USB con la ECU apagada** (CE-10)
- Sin firmware especial. ≤ 5 mA por pin y ≤ 25 mA en total; sin alimentación cruzada entre la PC y la ECU.
- [ ] Placa A
- [ ] Placa B

## 10. Ensayos de protección

Primero en una sola placa (por ejemplo B), con la ESP32 desenchufada. La otra se ensaya solo si la primera pasó.

**Procedimiento VR, después de cada ensayo:**
- **CAN:** `can_cantidad 1000` y `correr banco_can` (PASA).
- **LIN:** `par lin` hasta 1000 respuestas (`estado`).
- **Analógicas:** `correr ai`.
- **Digitales:** `do`.
- **Además:** comparar la corriente de entrada y las fugas con la línea base.

**PRO-00 · Línea base** (CE-14)
- `correr ai` (ADC a 50 % FS), `do 1 1` (VOH a 4 mA), corriente de entrada a 12 V y fuga de cada canal.
- [ ] Placa B
- [ ] Placa A

**PRO-01 · Inversión de polaridad** (CE-04, CE-14)
- −12, −24 y −32 V; −32 V durante 10 min. Corriente inversa ≤ 1 mA. VR al restaurar 12 V.
- [ ] Placa B
- [ ] Placa A

**PRO-02 · Sobretensión sostenida** (CE-02, CE-14)
- 32 V durante 60 s. Sin daño. VR al volver a 12 V.
- [ ] Placa B
- [ ] Placa A

**PRO-03 · Entradas analógicas a ±32 V** (CE-11, CE-10, CE-14)
- Con `correr ai`. ≤ 5 mA por pin y ≤ 25 mA en total; VR.
- [ ] Placa B
- [ ] Placa A

**PRO-04 · Entradas digitales a ±32 V** (CE-08, CE-09, CE-10, CE-14)
- Con `correr es`, selector en 3,3 V. ≤ 5 mA por pin y ≤ 25 mA en total; VR.
- [ ] Placa B
- [ ] Placa A

**PRO-05 · Salidas digitales a ±32 V y a GND** (CE-08, CE-09, CE-14)
- Con `do <n> 1` y `do <n> 0`. Sin daño en el buffer ni en el MCU; VR.
- [ ] Placa B
- [ ] Placa A

**PRO-06 · Líneas CAN a ±32 V** (CE-12, CE-14)
- Con `correr banco_can` transmitiendo. Sin daño, terminación dentro de su potencia; VR con 1000 tramas.
- [ ] Placa B
- [ ] Placa A

**PRO-07 · Línea LIN a ±32 V** (CE-12, CE-14)
- Con `correr lin_maestro` y después `correr lin_esclavo`. Sin daño; VR con 1000 tramas.
- [ ] Placa B
- [ ] Placa A

**PRO-08 · Fusible** (CE-05, CE-14)
- 2 A durante 10 min sin abrir; abre en sobrecarga. VR después de reemplazarlo.
- [ ] Placa B
- [ ] Placa A

**PRO-09 · Regresión final** (CE-14, CE-24)
- `integral 10` y repetir PRO-00. Todo conforme; fugas ≤ 2 veces la línea base.
- [ ] Placa B
- [ ] Placa A

---

## Cableado según la prueba

| Pruebas | CAN | LIN | Otros |
|---|---|---|---|
| Preparación, DIG, AI, `esclava ...` | CAN2 entre placas | — | — |
| COM-02 | Puente CAN1–CAN2 en J3 (sin CAN2 a la otra placa) | — | — |
| COM-03, EXT-04 | CAN2 entre placas (para `par`) | J3-1 entre placas, SW3 en la commander | — |
| INT-01, INT-02, EXT-06 | Puente CAN1–CAN2 en J3; CAN2 a la otra placa puede quedar | J3-1 entre placas | Cargas de 4 mA en las salidas |
| EXT-02, EXT-03 | CAN1–CAN1 y CAN2–CAN2 entre placas | — | — |
| ESP-01 a ESP-03 | CAN2 entre placas | — | ESP32 en J5/J6 |
