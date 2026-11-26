# Checklist del plan de pruebas

Orden de ejecución y forma de correr cada prueba de `plan_pruebas_ecu.pdf` con el firmware **Universal** ([`Apps/Universal.c`](Apps/Universal.c)). Los valores medidos van en la hoja de registro del plan (anexo B). Cada prueba indica entre paréntesis los criterios de éxito (CE) del anteproyecto que respalda; al final está la [trazabilidad completa CE → prueba](#trazabilidad-con-los-criterios-de-éxito).

**Convención de casillas**
- `[x]` terminada con resultado conforme.
- `[o]` ejecutada pero **no conforme** o con observación pendiente (se anota el motivo al lado). GitHub no la dibuja como casilla.
- `[ ]` pendiente.
- *(opcional)*: no hace falta para el CE, porque el nivel base se demuestra con una sola placa.

**Placas**
- **Placa A:** rol MAESTRO, con el adaptador CP2102 de la consola. Tiene montado el controlador de diodo ideal (LM74720).
- **Placa B:** rol ESCLAVA. **No tiene el controlador de diodo ideal**, por eso los ensayos con alimentación invertida o sobretensión (PRO-01, PRO-02 y el caso "VIN invertida" de PRO-09) se hacen en A. Todo se le manda desde la consola de A anteponiendo `esclava` al comando (`esclava do 1 1`, `esclava correr ai`, `esclava correr banco_can`…); su salida aparece en A con `[esclava]`. Mientras B corre una prueba que usa CAN2 (`banco_can`, `integral`) no recibe comandos: esas terminan solas y después llega su informe.
- En A, DO2 y DI2 son la consola (TX/RX del CP2102). Para medir DO2/DI2 como canal de E/S hay que hacerlo en la placa que esté de ESCLAVA (`esclava do 2 1`) o intercambiar roles con `rol`.
- En la consola, `plan` muestra un resumen de esta lista y `estado` da los contadores de la prueba que está corriendo.

Comandos y conexiones en detalle: [README del firmware](README.md#plan-de-pruebas).

---

## 0. Preparación

- [x] Firmware Universal grabado en A y en B (`Src/main.c` con `#include "../Apps/Universal.c"`)
- [x] Sketch [`ESP32_Pruebas`](ESP32/ESP32_Pruebas/ESP32_Pruebas.ino) grabado en la ESP32 (queda **desenchufada** hasta la sección 9)
- [x] Consola: CP2102 TX → J1-6 (DI2), RX → J1-1 (DO2), GND común, jumper de DI2 en 3V3, terminal a 115200 8N1
- [x] `rol maestro` en A (B queda ESCLAVA)
- [x] CAN2 entre placas (J3-3 con J3-3, J3-2 con J3-2, GND) y `esclava estado` contesta ACK
- [x] Factores de los divisores en A y en B: `factor` y `esclava factor` tienen que decir "del README" en los 4 canales (el firmware ya trae los valores de la tabla del README; no hace falta cargar `divisor`).

## 1. Verificaciones documentales y por simulación

**DOC-01 · Calificación AEC-Q de la BOM** (CE-25)
- Sin firmware. Columna de calificación en la BOM y justificación de cada excepción (incluir el fusible que reemplazó al PTC y el devkit ESP32).
- [x] Hecho

**DOC-02 · Simulaciones de transitorios, ESD e inyección** (CE-03, CE-13, CE-10)

Sin firmware. Simulación en LTspice, en el caso nominal y en el peor caso del componente que define el resultado (tensión de clamping máxima de los TVS, VF máxima de los diodos, 5 nH de pista). En CE-10 se agrega la VBR mínima para VIN.
Transitorios en VIN (CE-03). No se dispone de la norma ISO 7637-2, así que se usan los valores de referencia del equipo para 24 V, aplicados con la batería a 28 V:
Pulso 1: −300 V con 50 Ω, 1 ms (el pulso de −600 V / 2 ms del LM74720-Q1 queda como dato informativo).
Pulso 2a: +112 V con 2 Ω, 50 µs. Pulsos 3a y 3b: ±300 V con 50 Ω, 150 ns.
Load dump limitado a 58 V (RQ-04) durante 400 ms.
ESD (CE-13). Red de descarga de 150 pF / 330 Ω a ±8 kV en contacto, en un canal de cada tipo (VIN, AI, DI, DO, CAN, LIN, ESP32) y en J2, con la placa alimentada y sin alimentar. Los ±15 kV en aire se verifican con el rating IEC 61000-4-2 de cada TVS. La tensión residual se acota con la misma red cargada a ±15 kV.
Inyección (CE-10). Barrido en continua de −32 V a +32 V en cada borne, con la placa alimentada y sin alimentar, y en todos los AI y DI a la vez. Las líneas del ESP32 quedan fuera.
Criterio: tensión de cada componente por debajo de su máximo absoluto, energía de los TVS dentro de su curva de pulso, y tensión en el pin del MCU por debajo de VDD + 4 V (4,0 V en los TT_a). Por debajo de VSS rige la corriente inyectada (≤ 5 mA por pin y ≤ 25 mA en total), y el −0,3 V es informativo.
- [x] Transitorios y load dump (CE-03)
- [o] ESD (CE-13): no cumple en CAN, LIN, ESP32 y J2. Los cambios propuestos están en la sección 7 del informe.
- [x] Inyección en pines del MCU (CE-10)

## 2. Inspección sin energía

**INS-01 · Inspección visual y cortocircuitos en rieles** (CE-14)
- Sin firmware. Soldaduras, polaridades, fusible, VBAT unido a VDD, resistencia a GND de VIN, VINP, 5 V, 3V3 y VDDA.
- [x] Placa A
- [x] Placa B

**INS-02 · Cantidad de canales y testpoints** (CE-15, CE-19)
- Sin firmware. 4 AN, 4 DI, 4 DO, 2 CAN, 1 LIN; testpoints por cada señal inaccesible y por cada riel.
- [x] Placa A
- [x] Placa B

## 3. Alimentación y programación

**ALI-01 · Primera energización** (CE-06)
- Límite de 100 mA, subir VIN de 0 a 12 V despacio. 3V3 entre 2,97 V y 3,63 V.
- [x] Placa A
- [x] Placa B

**ALI-02 · Riel de 3,3 V en rango de entrada** (CE-01, CE-06)
- VIN a 8, 12, 24 y 28 V; 3V3 entre 2,97 V y 3,63 V.
- [x] Placa A
- [x] Placa B

**ALI-03 · Programación y depuración por SWD** (CE-20)
- ST-LINK en J2: leer el ID, grabar Universal y depurar con un breakpoint en `main`.
- [x] Placa A
- [x] Placa B

## 4. Entradas y salidas digitales

**DIG-01 · Salidas: funcionamiento y 4 mA** (CE-16, CE-24)
- Cableado: para VOH, 560 Ω de la salida a GND (≥ 4,3 mA a 2,4 V); para VOL, 680 Ω de la salida a 3V3 (≥ 4,3 mA a 0,4 V).
- Comandos: `do 1 1`, `do 1 0`, `do 3 1`, `do 3 0`, `do 4 1`, `do 4 0`; DO2 en la placa ESCLAVA con `esclava do 2 1` / `esclava do 2 0`.
- Criterio: cambia solo la salida pedida; VOH ≥ 2,4 V y VOL ≤ 0,4 V con la carga de 4 mA.
- [x] Placa A (DO1, DO3, DO4)
- [x] Placa B (DO1, DO3, DO4)
- [x] DO2 en Placa B (como esclava)

**DIG-02 · Entradas en las tres posiciones del divisor** (CE-24)
- Cableado: fuente en cada DI, jumper en 3V3, 12V y 24V. Para DI2, desconectar el TX del CP2102.
- Comandos: `correr es` (avisa cada cambio de entrada) y `di`.
- Criterio: lectura correcta en los 12 casos; solo cambia el canal estimulado.
- [x] Placa A
- [x] Placa B

**DIG-03 · Conmutación a 100 kHz** (CE-17)
- Cableado: salida al aire; cuadrada de 100 kHz en la entrada.
- Comandos: `pwm 1 100000 50`, `pwm 3 100000 50`, `pwm 4 100000 50` (DO2: `esclava pwm 2 100000 50`); `frec on` mide las entradas (de a una por vez).
- Criterio: tr y tf ≤ 1 µs (10 % del período) en salidas y entradas (osciloscopio); frecuencia medida = 100000 Hz.
- [x] Placa A (DO1, DO3, DO4)
- [x] Placa B (DO1, DO3, DO4)
- [x] DO2 en Placa B (como esclava)

## 5. Entradas analógicas

**AI-01 · Impedancia de entrada** (CE-18)
- Sin firmware especial. Fondo de escala a través de una R serie conocida, con y sin alimentación.
- Criterio: Z ≥ 100 kΩ **en cada canal**. En los canales con divisor resistivo, Z es la suma Rs + Rg.
- [x] Placa A
- [x] Placa B

**AI-02 · Lectura de los cuatro canales** (CE-24)
- Cableado: fuente y multímetro en la entrada.
- Comandos: `correr ai`; en cada canal aplicar 0, 50 y 100 % del fondo de escala y escribir `ref <canal> <mV del multímetro>`. Repetir con `adc_conv 2` (ADC2) y volver con `adc_conv 1`.
- Criterio: error ≤ 1 % FS (PASA en `ref`); solo cambia el canal estimulado.
- [x] Placa A con ADC1
- [x] Placa A con ADC2
- [x] Placa B con ADC1
- [x] Placa B con ADC2

## 6. Comunicaciones

**COM-01 · Terminación CAN seleccionable** (CE-22)
- Sin energía: CANH–CANL de cada canal con el switch cerrado (120 Ω ± 5 %) y abierto (decenas de kΩ).
- [x] Placa A
- [x] Placa B

**COM-02 · CAN y CAN FD entre CAN1 y CAN2** (CE-21, CE-24)
- Cableado: CANH1–CANH2 y CANL1–CANL2 en J3, las dos terminaciones activas, sin CAN2 hacia la otra placa.
- Comandos: `can_cantidad 100000`; `can_modo clasico` y `correr banco_can`; después `can_modo fd` y `correr banco_can`.
- Criterio: informe final con 0 perdidas, 0 errores y TEC = REC = 0 (PASA). Vdiff dominante entre 1,5 V y 3,0 V y recesivo < 0,5 V (osciloscopio con decodificación CAN FD, en lugar del analizador de bus del anteproyecto).
- [x] Placa A, CAN clásico
- [x] Placa A, CAN FD
- [x] Placa B, CAN clásico
- [x] Placa B, CAN FD

**COM-03 · LIN como commander y responder** (CE-23, CE-24)
- Cableado: LIN (J3-1) y GND entre placas; SW3 cerrado en la placa commander.
- Comandos: `par lin` (A commander) hasta unas 8 000 tramas; cambiar SW3 y `par lin_inv` (B commander). Ver los errores por tipo con `estado` y `esclava estado`.
- Criterio: 0 errores de checksum, sync o respuesta ausente; break, sync, PID y checksum correctos en el decodificador.
- [x] A commander, B responder
- [x] B commander, A responder

## 7. Integración

**INT-01 · Consumo propio** (CE-07)
- Comandos: `can_tasa max` e `integral` (carga de bus alta). Medir la corriente de entrada a 12 V, sin ESP32.
- Criterio: ≤ 300 mA.
- [x] Placa A
- [x] Placa B

## 8. Nivel extendido

**EXT-01 · Segunda placa verificada**
- La placa B completó INS a INT-01. Comparar corriente a 12 V, 3V3, VOH a 4 mA, ADC a 50 % FS y umbrales de entrada.
- [x] Hecho

**EXT-02 · CAN FD entre placas con arbitraje** (CE-21)
- Cableado: CAN1–CAN1 y CAN2–CAN2 entre placas, terminación en las puntas de cada bus.
- Comandos: `can_modo fd`, `can_tasa max`, `can_cantidad 100000`, `par canx`. Las dos placas transmiten a la vez: 0x120 (A) y 0x121 (B) por los dos buses.
- Criterio: los dos informes (A y `[esclava]`) con 0 perdidas y 0 errores; en el decodificador gana 0x120 y la 0x121 se retransmite.
- [x] Arbitraje correcto

**EXT-03 · Gateway entre buses** (CE-21)
- Cableado: igual que EXT-02.
- Comandos: `par gateway` (A reenvía CAN1 clásico a CAN2 FD; B manda y mide). Para carga alta, `can_tasa max`.
- Criterio: informe de `gw_fuente` con 0 pérdidas; registrar la latencia (mínima, promedio y máxima).
- [x] Hecho

**EXT-04 · Serigrafía**
- Sin firmware. Cada terminal y switch identificado; una persona ajena conecta la placa solo con la serigrafía y el manual.
- [x] Hecho

**EXT-05 · Gabinete de acrílico**
- Accesos a borneras, switches, SWD, testpoints y USB de la ESP32; sin conductores expuestos.
- Comandos: `integral 60` con el gabinete cerrado.
- Criterio: informe PASA, sin cortes por temperatura.
- [x] Hecho

## 9. Conector ESP32 (enchufar la ESP32 recién ahora)

Pertenece al nivel superior (Wi-Fi con ESP32). Como la interfaz está implementada, entra en CE-24 y en CE-10; alcanza con una placa.

**ESP-01 · Alimentación del módulo** (CE-06)
- Con el header vacío, medir 5 V, 3V3 y GND. Enchufar la ESP32 y escribir `esp wifi on`.
- Criterio: 3V3 del STM32 ± 10 % durante las ráfagas (osciloscopio) y sin resets. Un reset se nota en la consola por el mensaje de arranque.
- [x] Placa A
- [x] Placa B

**ESP-02 · UART y líneas digitales STM32 ↔ ESP32** (CE-24)
- Comandos: `esp ping`, `esp eco`, `esp enviar`, `esp recibir` (1 MB con CRC); `esp baud 921600` y repetir `esp eco`, `esp enviar` y `esp recibir`; `esp lineas`.
- Criterio: PASA en cada uno (0 errores de CRC a 921 600; las 4 líneas correctas).
- [x] Placa A
- [x] Placa B

**ESP-03 · ESP32 por USB con la ECU apagada** (CE-10)
- Sin firmware especial. ≤ 5 mA por pin y ≤ 25 mA en total; sin alimentación cruzada entre la PC y la ECU.
- [x] Placa A 
- [x] Placa B

## 10. Ensayos de protección

Con la ESP32 desenchufada. Para el nivel base alcanza con una placa por ensayo:
- **Alimentación (PRO-01, PRO-02):** en A, porque B no tiene el controlador de diodo ideal.
- **E/S y buses (PRO-03 a PRO-07):** en B. Ensayar A solo si B pasó (opcional).
- **Digitales sin alimentación (PRO-09):** sin alimentar en B; con VIN invertida en A.

**Procedimiento VR, después de cada ensayo** (CE-14):
- **CAN:** `can_cantidad 1000` y `correr banco_can` (PASA).
- **LIN:** `par lin` hasta 1000 respuestas (`estado`).
- **Analógicas:** `correr ai`.
- **Digitales:** `do` en las salidas y `correr es` / `di` en las entradas.
- **Además:** comparar la corriente de entrada y las fugas con la línea base.

**Cómo se verifica la inyección en el MCU (CE-10).** RQ-14 limita la corriente que entra al **pin del MCU**, no la que entra por la bornera: parte de la corriente de bornera se va por los clamps externos y el divisor. Esa corriente no se puede medir directamente sin intervenir la placa, así que se verifica de forma indirecta con la tensión del pin:
- En los pines FT del STM32G491 la inyección positiva no es posible y la negativa está limitada a −5 mA por pin y 25 mA en total (tabla 15 del datasheet). La negativa solo aparece cuando el pin baja de VSS.
- Con la falla aplicada, medir la tensión del pin en su testpoint. Si queda entre −0,3 V y VDD + 4 V (tabla 14), los diodos internos no conducen de forma apreciable y la inyección es despreciable.
- La corriente de bornera se anota como dato informativo; no es el criterio.

**PRO-01 · Inversión de polaridad** (CE-04, CE-14)
- −12, −24 y −32 V; −32 V durante 1 min. Corriente inversa ≤ 1 mA. VR al restaurar 12 V.
- [x] Placa A
- [ ] Placa B *(opcional; requiere montar el LM74720)*

**PRO-02 · Sobretensión sostenida** (CE-02, CE-14)
- 32 V durante 60 s (RQ-03). Sin daño. VR al volver a 12 V.
- [x] Placa A
- [ ] Placa B *(opcional; requiere montar el LM74720)*
- Ensayo adicional, más exigente que RQ-03: 50 V durante 60 s. Sin daño. VR al volver a 12 V.
- [x] Placa A, 50 V
- [ ] Placa B, 50 V *(opcional; requiere montar el LM74720)*

**PRO-03 · Entradas analógicas a ±32 V** (CE-11, CE-10, CE-14)
- Con `correr ai`. Tensión del pin del MCU dentro de −0,3 V a VDD + 4 V (recuadro de arriba); VR.
- [x] Placa B, inyección
- [x] Placa B, VR (`correr ai`)
- [x] Placa A 
- [x] Placa A, VR (`correr ai`)

**PRO-04 · Entradas digitales a ±32 V, placa alimentada** (CE-08, CE-09, CE-10, CE-14)
- Con `correr es`, selector en 3,3 V (peor caso). Tensión del pin del MCU dentro de −0,3 V a VDD + 4 V (recuadro de arriba); VR.
- [x] Placa B, inyección
- [x] Placa B, VR (`correr es` / `di`)
- [x] Placa A *(opcional)*
- [x] Placa A, VR (`correr es` / `di`)

**PRO-05 · Salidas digitales a ±32 V y a GND, placa alimentada** (CE-08, CE-09, CE-14)
- Con `do <n> 1` y `do <n> 0`. Sin daño en el buffer ni en el MCU; VR.
- [x] Placa B
- [x] Placa A

**PRO-06 · Líneas CAN a ±32 V** (CE-12, CE-14)
- Con `correr banco_can` transmitiendo. Sin daño, terminación dentro de su potencia; VR con 1000 tramas.
- [x] Placa B
- [x] Placa A 

**PRO-07 · Línea LIN a ±32 V** (CE-12, CE-14)
- Con `correr lin_maestro` y después `correr lin_esclavo`. Sin daño; VR con 1000 tramas.
- [x] Placa B
- [x] Placa A 

**PRO-08 · Limitación de corriente de entrada** (CE-05, CE-14)
- Sin ensayo. RQ-06 pide una limitación **rearmable** de 2 A. Las placas usan un fusible de 500 mA, que no se rearma. El consumo máximo del regulador de 5 V es 2 A.
- Se documenta en el informe que CE-05 no se cumple, con la justificación del cambio.
- [o] No cumple por diseño (fusible de 500 mA, no rearmable)

**PRO-09 · Canales digitales a −32 V con la placa sin alimentación** (CE-09, CE-10, CE-14)
- Completa la condición de RQ-13 "tanto con la placa alimentada como desconectada por inversión de polaridad". El caso alimentado está en PRO-04 y PRO-05.
- Preparación: fuente de −32 V con límite de corriente en 50 mA, referida al GND de la placa. Entradas con el selector en 3,3 V (peor caso).
- Caso 1, placa sin alimentar: VIN abierto. Aplicar −32 V durante 1 min en cada DI y en cada DO, de a un canal, y después en los 4 DI a la vez.
- Caso 2, VIN invertida (solo en A, que tiene el LM74720): VIN a −12 V y repetir el caso 1. El LM74720 desconecta la placa y los rieles quedan en ≈ 0 V.
- Durante la falla: medir la tensión del pin del MCU en el testpoint de cada canal (≥ −0,3 V) y la corriente que entrega la fuente de −32 V (dato informativo).
- Al terminar: retirar la falla, alimentar a 12 V y hacer el VR (`correr es` / `di` en las entradas y `do` en las salidas).
- Criterio: tensión de pin ≥ −0,3 V en todos los casos, sin daño y VR conforme.
- [x] Placa B, sin alimentar: entradas
- [x] Placa B, sin alimentar: salidas
- [x] Placa A, VIN invertida: entradas
- [x] Placa A, VIN invertida: salidas

---

## Trazabilidad con los criterios de éxito

Cada criterio de éxito de los cuadros 4 a 7 del anteproyecto se vincula con las pruebas de esta lista que lo respaldan. Las pruebas asignadas surgen de los CE indicados entre paréntesis en cada prueba, y el estado resume el de sus casillas.

| CE | Criterio (resumido) | Requisito | Verificación prevista | Pruebas | Estado |
|---|---|---|---|---|---|
| CE-01 | La placa opera con todas sus funciones en el rango de alimentación | RQ-01, RQ-02 | Ensayo con fuente de laboratorio | ALI-02 | Cumple |
| CE-02 | Tolera la sobretensión sostenida y recupera el funcionamiento | RQ-03 | Ensayo sobre hardware | PRO-02 | Cumple (placa A, 32 V y 50 V) |
| CE-03 | Ningún componente supera su máximo absoluto ante transitorios y load dump | RQ-04 | Simulación | DOC-02 | Cumple |
| CE-04 | Tolera la inversión de polaridad sin daño ni consumo apreciable | RQ-05 | Ensayo sobre hardware | PRO-01 | Cumple (placa A) |
| CE-05 | La limitación de corriente actúa y se rearma sin sustituir componentes | RQ-06 | Ensayo sobre hardware | PRO-08 | No cumple por diseño (nota 1) |
| CE-06 | El riel de la lógica queda en tolerancia en todo el rango de entrada y de carga | RQ-07 | Medición | ALI-01, ALI-02, ESP-01 | Cumple |
| CE-07 | El consumo propio no supera el valor especificado | RQ-08 | Medición | INT-01 | Cumple |
| CE-08 | Los canales digitales toleran el cortocircuito contra batería | RQ-12 | Ensayo sobre cada tipo de interfaz | PRO-04, PRO-05 | Cumple |
| CE-09 | Los canales digitales toleran la tensión negativa, con la placa alimentada y sin alimentar | RQ-13 | Ensayo sobre cada tipo de interfaz | PRO-04, PRO-05, PRO-09 | Cumple |
| CE-10 | La corriente inyectada en los pines del MCU queda dentro del límite | RQ-14 | Simulación y medición | DOC-02, ESP-03, PRO-03, PRO-04, PRO-09 | Cumple |
| CE-11 | Los canales analógicos toleran sobretensiones de ambas polaridades | RQ-17 | Ensayo sobre cada tipo de interfaz | PRO-03 | Cumple |
| CE-12 | Las líneas de bus toleran sobretensiones de ambas polaridades | RQ-21 | Ensayo sobre cada tipo de interfaz | PRO-06, PRO-07 | Cumple |
| CE-13 | Los terminales accesibles toleran las descargas electrostáticas | RQ-22 | Simulación | DOC-02 | No cumple en CAN, LIN, ESP32 y J2 (nota 2) |
| CE-14 | La placa recupera su funcionamiento al retirar la falla | RQ-23 | Ensayo sobre hardware | INS-01, PRO-01 a PRO-07, PRO-09 | Cumple (VR conforme en todos los ensayos ejecutados) |
| CE-15 | Cantidad especificada de canales digitales y analógicos | RQ-09, RQ-15 | Inspección | INS-02 | Cumple |
| CE-16 | Cada salida entrega y absorbe la corriente mínima con niveles válidos | RQ-10 | Medición | DIG-01 | Cumple |
| CE-17 | Las protecciones no degradan la conmutación a la frecuencia especificada | RQ-11 | Medición con osciloscopio | DIG-03 | Cumple |
| CE-18 | Impedancia de entrada analógica no inferior a la especificada | RQ-16 | Medición | AI-01 | Cumple |
| CE-19 | Todas las señales especificadas son accesibles por testpoints | RQ-24 | Inspección | INS-02 | Cumple |
| CE-20 | Programación y depuración por el conector previsto | RQ-25 | Ensayo sobre hardware | ALI-03 | Cumple |
| CE-21 | Los canales CAN o CAN-FD transmiten y reciben sin errores de trama | RQ-18 | Ensayo contra analizador de bus | COM-02, EXT-02, EXT-03 | Cumple (nota 3) |
| CE-22 | Terminación de bus seleccionable y de valor especificado | RQ-19 | Medición e inspección | COM-01 | Cumple |
| CE-23 | LIN opera como maestro y como esclavo | RQ-20 | Ensayo contra nodo LIN externo | COM-03 | Cumple (nota 4) |
| CE-24 | El firmware de prueba ejercita todas las interfaces implementadas | Todos | Ejecución del firmware de prueba | DIG-01, DIG-02, AI-02, COM-02, COM-03, ESP-02 | Cumple |
| CE-25 | Componentes con calificación AEC-Q, con las excepciones justificadas | — | Revisión de la lista de materiales | DOC-01 | Cumple |

**Notas**
1. RQ-06 pide una limitación rearmable de 2 A, y las placas usan un fusible de 500 mA que no se rearma. La justificación del cambio se documenta en el informe.
2. La verificación prevista es por simulación. Los cambios propuestos para los canales que no cumplen están en la sección 7 del informe de DOC-02.
