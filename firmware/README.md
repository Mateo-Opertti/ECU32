# Firmware

Programas de prueba para el bring-up de ECU32 (STM32G491VCT6).

El proyecto es CMake + Ninja y se usa con la extensión **STM32Cube para VS Code**. No usa HAL: los periféricos se configuran escribiendo directo en los registros (RM0440) y todo corre con el reloj por defecto, HSI16 = 16 MHz.

Hay dos firmwares completos, que se eligen en [`Src/main.c`](Src/main.c). Los dos se graban igual en las dos placas y se manejan desde una terminal serie o, en la esclava, desde la otra placa por CAN2, sin regrabar:

- **[`Apps/Pruebas.c`](Apps/Pruebas.c)**, el que queda activo: todo lo de Universal más lo que pide el plan de pruebas (`plan_pruebas_ecu.pdf`). Ver [Firmware Pruebas](#firmware-pruebas-plan-de-pruebas).
- **[`Apps/Universal.c`](Apps/Universal.c)**: las 15 pruebas de bring-up, sin los modos de ensayo del plan.

Las dos placas tienen que llevar el mismo firmware. Los programas sueltos anteriores, uno por prueba, siguen en [`Apps/legacy/`](Apps/legacy/) (ver [Programas sueltos](#programas-sueltos)).

## Conexiones

### Consola

La consola es el USART1 a **115200 8N1**, con niveles de 3,3 V. Entra por la bornera J1 a través del buffer U5:

| CP2102 | Placa | Pin STM32 |
|---|---|---|
| TX | J1-6 (DI2) | PE1 = USART1_RX |
| RX | J1-1 (DO2) | PE0 = USART1_TX |
| GND | GND | — |

- **Jumper de DI2 (columna E1) en 3V3.** En 12V o 24V el divisor deja la señal del CP2102 en menos de 1 V y la placa no lee nada.
- **No conectar el 3,3 V ni el 5 V del CP2102:** la placa se alimenta por J7.
- Con un solo CP2102 alcanza: va en la placa MAESTRO y la ESCLAVA se maneja por CAN2.
- En la placa que queda sin CP2102, DI2 queda al aire con el jumper en 3V3 y puede meter ruido en la consola. Conviene poner ese jumper en 12V: la entrada queda en bajo por la resistencia a GND. La consola descarta igual los bytes con error de trama o ruido y solo procesa líneas completas.

### CAN2 entre placas

J3-3 (CAN2_H) con J3-3, J3-2 (CAN2_L) con J3-2 y GND común, con una terminación de 120 Ω en cada punta del bus.

### Pruebas

- **CAN FD (`can_fd`):** unir CANH1 con CANH2 y CANL1 con CANL2 en J3, con una terminación de 120 Ω (SW2 o SW4). **Sacar antes el cable de CAN2 que va a la otra placa:** si no, el puente une los dos buses.
- **CAN entre dos placas (`can_maestro` / `can_esclavo`, `wakeup`):** unir CANH1, CANL1 y GND entre las placas, con SW2 en ON en las dos.
- **LIN (`lin_maestro` / `lin_esclavo`):** unir el pin LIN de J3 y GND entre las placas. En la placa que corre `lin_maestro`, SW3 cerrado (pull-up de master).
- **Loopback (`loopback`):** un cable de DO3 (J1-4, PC10) a DI4 (J1-8, PC11), con el jumper de DI4 en 3V3.
- **ESP32 (`esp32`):** módulo DevKit V1 en J5/J6, sin cables extra. El sketch [`ESP32_LED_Serial.ino`](ESP32/ESP32_LED_Serial/ESP32_LED_Serial.ino) se graba por USB desde Arduino, con la placa **DOIT ESP32 DEVKIT V1**.
- **ADC (`adc`):** cada entrada de J4 tiene su propio rango: AN1 de 0 a 3,3 V, AN2 de 0 a 5 V, AN3 de 0 a 12 V y AN4 de 0 a 24 V. La consola muestra la tensión en el pin del MCU (0 a 3,3 V): para la del conector hay que multiplicarla por el factor de cada canal (ver el [README principal](../README.md#entradas-analógicas)).

## Primer uso

1. Grabar el mismo firmware en las dos placas (ver [Compilar y grabar](#compilar-y-grabar)).
2. Conectar el CP2102 a una placa y abrir una terminal serie a 115200 8N1 (PuTTY, Tera Term, el monitor serie de VS Code o de Arduino). Al resetear la placa aparece la causa del reset y el rol.
3. Escribir `rol maestro`. El rol queda guardado en la flash. La otra placa, sin rol grabado, arranca como ESCLAVA.
4. Unir CAN2 entre las placas y escribir `esclava estado`. La esclava tiene que contestar con un ACK.

## Comandos

Una línea por comando, terminada en Enter (`\r` o `\n`), sin distinguir mayúsculas. La placa hace eco de lo que se escribe.

| Comando | Qué hace |
|---|---|
| `ayuda` | Lista los comandos |
| `lista` | Muestra las pruebas numeradas |
| `correr <n\|nombre>` | Para la prueba actual y arranca la elegida. Acepta el número, el nombre corto (`can_maestro`) o el del programa original (`CAN_Maestro`) |
| `parar` | Para la prueba y deja la placa en estado seguro |
| `estado` | Rol, prueba actual, tiempo corriendo, contadores OK/error y detalles de la prueba. También el estado del enlace CAN2, el reloj FDCAN y los errores de la consola |
| `rol [maestro\|esclava]` | Sin argumento muestra el rol; con argumento lo cambia y lo guarda en la flash |
| `esclava correr <n\|nombre>` | Solo en el MAESTRO. Le pide la prueba a la esclava por CAN2 y muestra la respuesta, o `sin respuesta` |
| `esclava parar` / `esclava estado` | Solo en el MAESTRO. Ídem para parar o consultar |
| `esclava resumen on\|off` / `esclava traza on\|off` | Solo en el MAESTRO. Prende o apaga el resumen o la traza en la esclava |
| `par can` | Solo en el MAESTRO. `can_maestro` en esta placa y `can_esclavo` en la esclava |
| `par lin` | Solo en el MAESTRO. `lin_maestro` en esta placa y `lin_esclavo` en la esclava |
| `par combo` | Solo en el MAESTRO. `combo_maestro` en esta placa y `combo_esclavo` en la esclava |
| `par wakeup` | Solo en el MAESTRO. La esclava corre `wakeup` y, 3,5 s después de su ACK (cuando ya se durmió), esta placa arranca `can_maestro` para despertarla |
| `duty <0-100>` | Duty del PWM en %. Se puede cambiar con la prueba corriendo |
| `canal <1-4>` | Entrada AN que muestra el ADC en los LEDs |
| `resumen on\|off` | Reporte de cada prueba una vez por segundo (arranca en on). Sin argumento muestra cómo está |
| `traza on\|off` | Cada trama CAN y LIN en hex, para depurar (arranca en off). Sin argumento muestra cómo está |

## Pruebas

| # | Nombre | Programa original | Qué prueba | Cómo se ve que funciona |
|---|---|---|---|---|
| 1 | `leds` | `LEDs.c` | LED1 (PF9, rojo) y LED2 (PF10, verde) | Los dos LEDs parpadean juntos cada 500 ms |
| 2 | `digital` | `Digital.c` | Entrada digital DI4 (PC11) | Alto: LED verde. Bajo: LED rojo. La consola informa DI4 (y DI1, DI3) en cada cambio y una vez por segundo |
| 3 | `salidas` | `Digital_Salidas.c` | Salida digital DO3 (PC10) | La salida queda en alto y el LED verde prendido. Una vez por segundo la consola informa lo que lee el pin PC10: `OK` si está en alto, `FALLA` si no sube |
| 4 | `loopback` | `Digital_Loopback.c` | Lazo salida → entrada: cable de DO3 (PC10) a DI4 (PC11) | DO3 cambia cada 500 ms y la consola informa el nivel ordenado, el del pin PC10, el de DI4 y `OK` o `FALLA`. El LED sigue el nivel leído |
| 5 | `adc` | `ADC.c` | AN1 – AN4 con el ADC1, cada 100 ms | Una vez por segundo la consola imprime la tensión en el pin del ADC, en voltios (`AN1 1.10 V ...`), y el color de LEDs. LEDs según `canal`: < 1,1 V rojo, 1,1 – 2,2 V los dos, > 2,2 V verde |
| 6 | `pwm` | `PWM.c` | PWM de 500 kHz por DO3 (PC10) con el TIM8 (CH1N, AF4) | Duty con `duty` (arranca en 50 %). LED verde prendido |
| 7 | `esp32` | `ESP32_Serial.c` | Enlace con la ESP32 del zócalo: USART2 a 115200 (PD5 → GPIO16). Manda `ON` y `OFF` | El LED interno de la ESP32 parpadea igual que el verde del STM32, cada 500 ms |
| 8 | `can_fd` | `CAN_FD.c` | FDCAN1 transmite y FDCAN2 recibe: 500 kbit/s nominal, 2 Mbit/s en datos (BRS), 64 bytes, ID 0x123. **Solo local** | El verde alterna con cada trama correcta. El rojo indica error o 500 ms sin recibir |
| 9 | `can_maestro` | `CAN_Maestro.c` | CAN FD entre dos placas por FDCAN1: 500 kbit/s / 2 Mbit/s, 64 bytes. ID 0x100: maestro → esclavo | El verde alterna con cada respuesta correcta. Rojo 1 s ante un error, fijo si pasan 500 ms sin una buena |
| 10 | `can_esclavo` | `CAN_Esclavo.c` | Ídem. ID 0x101: esclavo → maestro, con cada byte invertido | Juego de luces que avanza con cada trama correcta. Rojo 1 s ante un error, fijo si pasan 500 ms sin una buena |
| 11 | `wakeup` | `CAN_Esclavo_WakeUp.c` | Despertar desde Standby por actividad en CAN1 (WKUP3, PE6) o en CAN2 (WKUP2, PC13) | Rojo 3 s y se duerme con los LEDs apagados. Al despertar, el verde alterna cada 250 ms |
| 12 | `lin_maestro` | `LIN_Maestro.c` | LIN 2.x a 19200 baud entre dos placas, USART3 en modo LIN, checksum mejorado. ID 0x10: maestro → esclavo | El verde alterna con cada respuesta correcta. El rojo indica error o 500 ms sin una buena |
| 13 | `lin_esclavo` | `LIN_Esclavo.c` | Ídem. ID 0x11: esclavo → maestro | El verde alterna con cada trama correcta. El rojo indica error o 500 ms sin una buena |
| 14 | `combo_maestro` | — (nueva) | ADC + LIN + LEDs entre dos placas. Commander LIN: manda sus AN1–AN4 (ID 0x20) y pide los de la otra placa (ID 0x21), alternando cada 50 ms | Los LEDs muestran la entrada de la **otra** placa elegida con `canal`: < 1,1 V rojo, 1,1 – 2,2 V los dos, > 2,2 V verde. Sin LIN, los dos titilan cada 100 ms |
| 15 | `combo_esclavo` | — (nueva) | Ídem, del lado que responde por LIN | Ídem |

El rol de control (MAESTRO / ESCLAVA) no tiene que ver con las pruebas: cualquier placa puede correr cualquier prueba, incluidas `can_maestro` y `can_esclavo`.

## Qué informan las pruebas por la consola

Hay tres niveles:

| Nivel | Cuándo sale | Se controla con |
|---|---|---|
| **Resumen** | Una línea por segundo por prueba | `resumen on\|off` (arranca en on) |
| **Avisos** | Apenas pasa algo; como mucho uno por segundo de cada tipo | Siempre activos |
| **Traza** | Cada trama CAN o LIN, en hex | `traza on\|off` (arranca en off) |

**Resumen de las pruebas de CAN y LIN:**

```
can_maestro: tx 10/s  resp OK 10/s  err 0 total  RTT 412 us | CAN1 TEC 0 REC 0, error activo
can_esclavo: rx OK 10/s  err 0 total  resp 10/s  seq 137  patron escalera paso 2 | CAN1 TEC 0 REC 0, error activo
can_fd: tx 10/s  rx OK 10/s  err 0 total | CAN1 TEC 0 REC 0, error activo | CAN2 TEC 0 REC 0, error activo
lin_maestro: 0x10 enviadas 10/s  0x11 OK 10/s  err 0 total | ultima respuesta 25 DA A5 5A chk OK | eco err 0
lin_esclavo: 0x10 OK 10/s (contador 25)  0x11 respondidas 10/s  err 0 total | eco err 0
```

- **RTT:** tiempo entre que el maestro manda la trama y recibe la respuesta, en µs.
- **TEC / REC:** contadores de error de transmisión y recepción del FDCAN. Si suben, el bus se está degradando. Con más de 127 el nodo pasa a error pasivo y con más de 255, a bus off.
- **Patrón y paso** de `can_esclavo`: el paso del juego de luces que corresponde al último seq recibido.

**Avisos**, por ejemplo:

```
can_maestro: sin respuesta hace 500 ms y nadie confirma las tramas: revisar cable CAN1, terminacion (SW2) y que la otra placa corra can_esclavo
can_maestro: el esclavo volvio (12 tramas sin respuesta)
can_maestro: respuesta mal: byte 12 = 0x3A, se esperaba 0xC5
can_maestro: CAN1 pasa de error activo a error pasivo (ultimo error: ACK (nadie confirma), TEC 128 REC 0, error pasivo)
can_esclavo: salto de secuencia: llego 40, se esperaba 37 (se perdieron tramas o se reinicio el maestro)
lin_maestro: respuesta 0x11 con checksum mal: llego 0x4B, se esperaba 0x4C
lin_maestro: el esclavo no responde a 0x11 (llegaron 0 de 5 bytes)
lin_maestro: no volvio el eco del byte 0 (0x55): el bus no se mueve (EN = PD10, transceptor o bus a GND)
lin_esclavo: sync invalido: llego 0x00, se esperaba 0x55 (bit rate distinto o ruido)
```

**Traza:**

```
CAN1 tx 0x100 [64] 25 26 27 28 29 2A 2B 2C .. 63 64
CAN1 rx 0x101 [64] DA D9 D8 D7 D6 D5 D4 D3 .. 9C 9B  RTT 405 us
LIN 0x10 tx: 25 26 27 28  chk 1E
LIN 0x11 rx: 25 DA A5 5A  chk 4C  OK
```

`resumen off` también calla los reportes periódicos de `digital`, `salidas`, `loopback` y `adc`. Los cambios de DI4 y las fallas siguen saliendo. En la esclava, todo esto llega a la consola del maestro con `[esclava]`. Para prenderlo o apagarlo allá se usa `esclava resumen on|off` y `esclava traza on|off`.

## Salida de la esclava en la consola del maestro

Con el CP2102 conectado solo al MAESTRO, igual se ve todo lo que imprime la ESCLAVA. Ella manda por CAN2 todo lo que sale por su consola: el mensaje de arranque, la causa del reset, lo que informa cada prueba y los avisos. El maestro lo imprime por su consola (USART1, PE0/PE1) con el prefijo `[esclava]`:

```
esclava correr adc
esclava: ACK  rol ESCLAVA  prueba 5 adc  corriendo 0 s  OK 0  error 0
[esclava] [CAN2] el maestro pide: correr 5
[esclava] Corriendo 5: adc
[esclava] adc: AN1 1.10 V  AN2 0.00 V  AN3 2.31 V  AN4 0.00 V  | LEDs (AN1): rojo y verde
```

La esclava también sigue imprimiendo por su propia consola, por si tiene un CP2102 conectado. El reenvío está en la capa de consola, así que funciona igual para cualquier prueba nueva, con una condición: **la prueba tiene que imprimir con `cputs` o `cprintf`, nunca escribiendo directo en el USART1.** Mientras corre `can_fd` el enlace está suspendido y no se reenvía nada. Si se pierden tramas de texto, el maestro lo avisa con `[esclava] (se perdieron N tramas de texto)`.

## Cómo probar cada cosa

- **Consola:** `ayuda` tiene que responder. Si no aparece nada, revisar el jumper de DI2 (3V3) y que TX y RX no estén cruzados al revés. `estado` muestra cuántos bytes llegaron con error.
- **Rol:** `rol maestro`, resetear la placa y verificar que el arranque diga `Rol: MAESTRO`. Lo mismo después de cortar la alimentación.
- **Enlace CAN2:** desde el maestro, `esclava estado`. Desconectando CAN2 tiene que decir `sin respuesta` después de 4 intentos (400 ms).
- **Dos maestros:** con las dos placas en `rol maestro`, `esclava estado` responde con un NACK y las dos consolas avisan que hay dos maestros en el bus.
- **LEDs, Digital, Salidas, PWM, ADC:** `correr <nombre>` y mirar los LEDs, el multímetro o el osciloscopio. Con el PWM corriendo, `duty 10`, `duty 90`, etc. Con el ADC corriendo, `canal 1` a `canal 4`.
- **Loopback:** con el cable puesto, todas las líneas tienen que decir `OK`. Sacando el cable, las lecturas con DO3 en alto tienen que dar `FALLA`.
- **ESP32:** `correr esp32` en la placa con el módulo (por consola o con `esclava correr esp32`).
- **CAN entre placas:** `par can` desde el maestro. Cada segundo llega el resumen de las dos puntas (el de la esclava con `[esclava]`), y cualquier falla aparece como aviso. `traza on` muestra cada trama.
- **LIN:** `par lin` desde el maestro (con SW3 cerrado en el maestro). Igual que CAN: resumen de las dos puntas, avisos y `traza on`.
- **Combo (ADC + LIN + LEDs + CAN2):** con el cableado de LIN, `par combo` desde el maestro. Mové la tensión de una entrada AN de una placa, por ejemplo con un potenciómetro, y cambian los LEDs de la **otra**. Cada segundo, el maestro imprime las tensiones de las dos puntas: las suyas y las que recibe por LIN. La esclava manda su propio resumen por CAN2, que aparece con `[esclava]`:
  ```
  combo_maestro: aca 1.10 0.00 2.31 0.00 V | esclava 0.52 3.30 0.00 0.00 V | LIN tx 10/s rx OK 10/s err 0 | LEDs: esclava AN1 = rojo
  [esclava] combo_esclavo: aca 0.52 3.30 0.00 0.00 V | maestro 1.10 0.00 2.31 0.00 V | LIN tx 10/s rx OK 10/s err 0 | LEDs: maestro AN1 = rojo y verde
  ```
  `canal <1-4>` elige qué entrada de la otra placa se ve en los LEDs; en la esclava queda AN1. Si se desconecta el LIN, los dos LEDs de cada placa titilan juntos y aparece un aviso.
- **CAN FD local:** sacar el cable de CAN2 entre placas, poner el puente CAN1–CAN2 y ejecutar `correr can_fd` en la consola de esa placa. Mientras corre, la consola avisa que el enlace CAN2 está suspendido. Con `parar` el enlace vuelve.
- **Wake-up:** `par wakeup` desde el maestro, con CAN1 unido entre las placas. La esclava muestra el rojo 3 s y se duerme; a los 3,5 s el maestro arranca `can_maestro`, que la despierta, y el verde de la esclava empieza a alternar cada 250 ms. Al despertar, la consola de la esclava imprime `Reset: salio de Standby, lo desperto WKUP3 (CAN1)`. Para repetir: `par wakeup` de nuevo. El rojo del maestro queda prendido, porque la esclava despierta no contesta las tramas: es lo esperado.
- **Wake-up por CAN2:** `esclava correr wakeup`, esperar a que se duerma y mandar `esclava estado`. El primer intento la despierta por WKUP2 y uno de los reintentos recibe el ACK con la prueba `wakeup` despierta.

## Protocolo CAN2

CAN FD sin cambio de bitrate: toda la trama va a 500 kbit/s, el mismo bit timing nominal que el resto de las pruebas CAN. Se usa FD y no CAN clásico para que la respuesta a `estado`, de 20 bytes, entre en una sola trama. FDCAN2 tiene un filtro que solo deja pasar los IDs 0x700 a 0x70F, y no retransmite solo: los reintentos los maneja el protocolo.

**Comando, ID 0x700 (maestro → esclava), 8 bytes:**

| Byte | Contenido |
|---|---|
| 0 | Comando: 1 = correr, 2 = parar, 3 = estado, 4 = traza, 5 = resumen, 6 = parámetro |
| 1 | Número de prueba (correr), 1 = on / 0 = off (traza, resumen) o clave (parámetro) |
| 2 | Número de secuencia |
| 3 – 6 | Valor del parámetro (little endian) |
| 7 | 0 |

El maestro puede encolar hasta 8 pedidos (por ejemplo, varios parámetros y después `correr`): salen de a uno, cada uno cuando llega la respuesta del anterior.

**Respuesta, ID 0x701 (esclava → maestro), 20 bytes:**

| Byte | Contenido |
|---|---|
| 0 | 0x06 = ACK, 0x15 = NACK |
| 1 | Secuencia del comando que responde |
| 2 | Rol: 1 = MAESTRO, 2 = ESCLAVA |
| 3 | Prueba actual (0 = ninguna) |
| 4 | Código de error: 0 ok, 1 comando desconocido, 2 prueba inválida, 3 la prueba es solo local, 4 la placa que recibió el comando también es MAESTRO |
| 5 | Bit 0: los contadores de los bytes 12 – 19 son válidos |
| 8 – 11 | Segundos que lleva corriendo la prueba (little endian) |
| 12 – 15 | Contador OK de la prueba |
| 16 – 19 | Contador de errores de la prueba |

**Texto, ID 0x702 (esclava → maestro), hasta 64 bytes:**

| Byte | Contenido |
|---|---|
| 0 | Secuencia de texto (el maestro detecta tramas perdidas) |
| 1 | Bit 0: primera trama desde que arrancó la esclava |
| 2 | Cantidad de caracteres, de 1 a 61 |
| 3 – 63 | Caracteres |

La esclava manda cada línea apenas termina (`\n`), o un pedazo si junta 61 caracteres o pasan 20 ms. Siempre deja un lugar libre en el TX FIFO para las respuestas 0x701.

- El maestro manda el comando y, si no llega la respuesta con su secuencia en 100 ms, lo reintenta hasta 3 veces más. Después muestra `sin respuesta`.
- La esclava recuerda la última respuesta. Si recibe el mismo comando con la misma secuencia dentro de 1 s (se perdió el ACK), la reenvía sin ejecutarlo de nuevo.
- Si una placa con rol MAESTRO recibe un 0x700, avisa por su consola que hay dos maestros y contesta con NACK, código 4. El otro maestro también lo avisa.

## Recursos compartidos y conflictos

| Recurso | Quién lo usa | Cómo se resolvió |
|---|---|---|
| USART1, PE0, PE1 | Consola | Reservados: ninguna prueba los toca |
| PE8 (1/OE), PE10 (2/OE) | Consola (pasa por U5) | Se bajan al arrancar y quedan así. Ninguna prueba los vuelve a subir. Digital, Salidas, Loopback y PWM asumen U5 habilitado |
| PE5, PD2, PC11 (DI1, DI3, DI4) | U5 los maneja siempre | Quedan como entradas: ninguna prueba puede ponerlos como salida |
| GPIOB, GPIOC, GPIOE | Consola, /OE y pruebas | Solo BSRR o read-modify-write de los bits propios, nunca un registro completo |
| Reloj FDCAN (`RCC_CCIPR.FDCANSEL`), `FDCAN_CONFIG.CKDIV`, enable en RCC | FDCAN1 (pruebas) y FDCAN2 (enlace) | Se fija una sola vez al arrancar: HSE de 16 MHz o, si no arranca, PCLK1 (HSI16). CKDIV queda en /1. Ninguna prueba resetea el FDCAN por RCC |
| Message RAM (SRAMCAN) | FDCAN1 y FDCAN2 | Cada instancia borra y usa solo su mitad (0x4000A400 y 0x4000A750) |
| FDCAN2 | Enlace y `can_fd` | `can_fd` solo se corre desde la consola de su placa (por CAN2 responde NACK, código 3), suspende el enlace mientras corre y lo avisa. Al pararla, FDCAN2 vuelve al enlace |
| USART2 | `esp32` | Sin conflicto: la consola va por USART1 |
| USART3 | LIN | No se usa para la consola |
| Standby | `wakeup` | Ver abajo |

**Wake-up y Standby:** el Standby borra la RAM, corta la consola y el enlace CAN2 (los /OE vuelven a alto por R53/R54) y despierta con un reset. Antes de dormir, la prueba guarda en `TAMP_BKP0R` que estaba corriendo y pone los dos transceptores CAN en standby, con WKUP3 (PE6, desde RXD1) y WKUP2 (PC13, desde RXD2) habilitados. Al arrancar, el firmware imprime la causa del reset (`PWR_SR1`: SBF y WUFx, y `RCC_CSR`). Si lo despertó un WKUP, vuelve a `wakeup` ya despierta y con la consola recuperada. Si en cambio la placa se resetea mientras duerme, arranca sin prueba. La app original se volvía a dormir, y eso dejaba solo 3 s para mandar un comando o conectar el ST-LINK.

**Rol y parámetros en flash:** el G491 tiene un solo banco con páginas de 2 KB. La última, la página 127 (0x0803F800 – 0x0803FFFF), está reservada en [`stm32g491xc_flash.ld`](stm32g491xc_flash.ld) y el programa no puede ocupar esa zona. Ahí van el rol y, en el firmware Pruebas, los factores de los divisores del ADC y el autoarranque. Cada cambio graba el siguiente doble-word libre. La página se borra recién cuando se llena, cada 256 cambios, y entonces se reescribe lo vigente. Mientras se borra, el CPU se frena unos 22 ms, porque ejecuta desde la misma flash.

## Arranque

Antes de cualquier prueba, en este orden:

1. DO1 (PB9), DO3 (PC10) y DO4 (PC12) como salidas en bajo.
2. USART1 habilitado (UE, TE, RE) con PE0/PE1 en AF7, para que el TX de la consola quede en reposo en alto.
3. Bajar PE8 (1/OE).
4. Bajar PE10 (2/OE).

Después: LEDs apagados, CAN1 en standby (PD14 alto), LIN en sleep (PD10 bajo), lectura del rol, reloj FDCAN, enlace CAN2 (PD15 bajo) y causa del reset. La placa arranca sin ninguna prueba, salvo que vuelva de Standby.

Al parar una prueba, su `deinit()` apaga sus periféricos y transceptores. Después el firmware deja DO1, DO3 y DO4 en bajo y los LEDs apagados. La consola y los /OE no se tocan.

## Compilar y grabar

1. Abrir la carpeta `firmware/` en VS Code con la extensión STM32Cube instalada. Las versiones del toolchain (GCC 14.3.1, CMake, Ninja, ST-LINK GDB server) están fijadas en [`.settings/`](.settings/) y la extensión las descarga sola.
2. Elegir el preset **Debug** y compilar. La salida queda en `build/Debug/`, que no se versiona.
3. Conectar el ST-LINK a J2 y lanzar **STM32Cube: STM32 Launch ST-Link GDB Server** (F5). Compila, graba y frena en `main`.

Al grabar solo se borran las páginas que ocupa el programa, así que el rol se mantiene. Un borrado completo de la flash (*full chip erase*) también borra el rol y la placa vuelve a ser ESCLAVA.

## Firmware Pruebas (plan de pruebas)

[`Apps/Pruebas.c`](Apps/Pruebas.c) tiene las 15 pruebas de Universal, con los mismos números, y agrega lo que el plan de pruebas le pide al firmware (tabla 1 del plan). El comando **`plan`** muestra en la consola qué correr para cada prueba del plan, y `ayuda` explica cada comando nuevo.

Diferencias con Universal: el ADC promedia 256 muestras y mide VDDA con VREFINT, y el LIN arranca a 20 kbit/s.

### Pruebas nuevas

| # | Nombre | Plan | Qué hace |
|---|---|---|---|
| 16 | `es` | DIG-01/02/03 | Salidas en nivel o PWM configurable, entradas con aviso de cada cambio y frecuencia medida |
| 17 | `ai` | AI-02 | Las 4 entradas en voltios **del conector** y en % del fondo de escala, con promedio de N y VDDA por VREFINT |
| 18 | `banco_can` | COM-02, VR | CAN1 ↔ CAN2 en la misma placa (puente en J3), los dos sentidos a la vez, clásico o FD, N tramas. Informe PASA/FALLA. Solo local |
| 19 | `can_placas` | EXT-02 | Las dos placas transmiten a la vez por CAN1 y CAN2 con IDs distintos (0x120 el MAESTRO, 0x121 la ESCLAVA): arbitraje sin pérdidas |
| 20 | `gateway` | EXT-03 | Reenvía lo que llega por CAN1 en CAN clásico (0x130) a CAN2 como CAN FD con BRS (0x131) |
| 21 | `gw_fuente` | EXT-03 | Manda por CAN1 y mide en CAN2 las pérdidas y la latencia ida y vuelta (mínima, promedio y máxima) |
| 22 | `esp_uart` | ESP-01/02/03 | Enlace con la ESP32: eco, 1 MB con CRC-32 en cada sentido a 115 200 y 921 600 baud, líneas auxiliares, reset y Wi-Fi continuo |
| 23 | `integral` | INT-01/02, PRO-09, EXT-06 | Todo a la vez durante N minutos: PWM de 100 kHz en las salidas, entradas, ADC, CAN1 ↔ CAN2 y LIN contra la otra placa. Informe PASA/FALLA. Solo local |

### Comandos nuevos

| Comando | Qué hace |
|---|---|
| `plan` | Qué correr para cada prueba del plan |
| `do <1-4> <0\|1> [s]` | Fija una salida. DO2 es el TX de la consola: en reposo ya está en alto (sirve para medir VOH); `do 2 0 [s]` la presta `s` segundos (10 por defecto) y mientras tanto la consola no imprime |
| `pwm <1-4> <Hz> [duty] [s]` / `pwm <n> off` | PWM en una salida: DO1 con TIM4, DO3 y DO4 con TIM8 (comparten frecuencia), DO2 con TIM16 (prestada `s` segundos) |
| `di` | Nivel de las 4 entradas |
| `frec on\|off` | Frecuencia de cada entrada, contando flancos de subida en ventanas de 1 s |
| `promedio <1-256>` | Muestras promediadas por canal del ADC |
| `divisor <c> <Rs> <Rg>` | Carga el divisor del canal `c` en ohm. El factor queda en la flash; sin cargar se usa el nominal (fondo de escala / 3,3 V) |
| `factor` | Muestra el factor de cada canal y si es nominal o medido |
| `ref <c> <mV>` | Compara la lectura con el multímetro y da el error en % del fondo de escala (criterio ≤ 1 %) |
| `can_modo clasico\|fd` | CAN clásico de 8 bytes o CAN FD de 64 bytes con BRS (FD por defecto) |
| `can_tasa <n\|max>` | Tramas por segundo por sentido; `max` = lo más rápido posible (carga de bus alta) |
| `can_cantidad <n>` | Tramas por sentido antes de terminar (100 000 por defecto; 0 = sin fin) |
| `lin_baud <n>` | Velocidad LIN (20 000 por defecto) |
| `integral [min] [todas]` | Corre la integral `min` minutos (10 por defecto); `todas` también pone PWM en DO2 |
| `esp ping\|baud <n>\|eco [n]\|enviar [bytes]\|recibir [bytes]\|lineas\|reset\|wifi on\|off` | Operaciones con la ESP32 (arranca `esp_uart` si hace falta) |
| `autoarranque <prueba\|no>` | Prueba que arranca sola al encender (queda en la flash); sirve para los arranques en frío de INT-01 |
| `par canx` / `par gateway` / `par lin_inv` | (MAESTRO) CAN entre placas, gateway (esta placa gateway, la otra fuente) y LIN con los roles invertidos |
| `esclava sync` | (MAESTRO) Le pasa a la esclava `can_*`, `lin_baud` e `integral`. Al cambiarlos en el MAESTRO se copian solos |

### Qué correr para cada prueba del plan

| Prueba | Cableado | Comandos |
|---|---|---|
| ALI-03 | ST-LINK en J2 | Grabar este firmware y depurar (breakpoint en `main`) |
| DIG-01 | Carga de 825 Ω en la salida | `do 1 1`, `do 1 0`, `do 3 1`… DO2: medir VOH en reposo y `do 2 0 30` para VOL |
| DIG-02 | Fuente en la entrada y jumper en cada posición | `correr es`: avisa cada cambio de entrada; `di` muestra los niveles. Con una rampa lenta, el aviso marca el umbral |
| DIG-03 | Carga de 4 mA en la salida; cuadrada de 100 kHz en la entrada | `pwm 1 100000 50` (y 3, 4); `frec on` mide las entradas de a una |
| AI-02 | Fuente y multímetro en la entrada | `divisor <c> <Rs> <Rg>` una vez por canal; `correr ai`; `ref <c> <mV del multímetro>` en 0, 50 y 100 % FS |
| COM-02 | CANH1–CANH2 y CANL1–CANL2 en J3, las dos terminaciones; **sin** CAN2 a la otra placa | `can_modo clasico`, `correr banco_can`; después `can_modo fd`, `correr banco_can` (100 000 tramas por sentido) |
| COM-03 | LIN (J3-1) y GND entre placas, SW3 cerrado en la commander | `par lin` (esta placa commander), después `par lin_inv`. `estado` da los errores por tipo |
| INT-01 | Puente CAN1–CAN2 en J3, LIN a la otra placa, cargas en las salidas | `integral 10` (o `integral 10 todas`) a 8, 12, 24 y 28 V. Arranques en frío: `autoarranque integral` (y `autoarranque lin_esclavo` en la otra) |
| INT-02 | Ídem | `can_tasa max`, `integral` y medir la corriente de entrada |
| EXT-02 | CAN1–CAN1 y CAN2–CAN2 entre placas, terminación en las puntas | `can_modo fd`, `par canx`. Las dos placas informan al terminar |
| EXT-03 | Ídem | `par gateway`: esta placa reenvía y la otra mide pérdidas y latencia |
| EXT-04 | Como COM-03 | `par lin` y `par lin_inv`, 30 min cada uno, a 12 V y 24 V |
| EXT-06 | Como INT-01, con el gabinete cerrado | `integral 60` |
| ESP-01 / ESP-03 | ESP32 en J5/J6 con el sketch [`ESP32_Pruebas`](ESP32/ESP32_Pruebas/ESP32_Pruebas.ino) | `esp wifi on` y medir los rieles y el consumo |
| ESP-02 | Ídem | `esp ping`, `esp eco`, `esp enviar`, `esp recibir`, `esp baud 921600` y repetir, `esp lineas` |
| VR (PRO) | Según el canal | CAN: `can_cantidad 1000`, `correr banco_can`. LIN: `par lin` hasta 1000 respuestas (`estado`) |
| PRO-00 / PRO-09 | — | `correr ai` (ADC a 50 % FS), `do` (VOH a 4 mA), `integral` |

Las demás pruebas del plan (DOC, INS, ALI-01/02, AI-01, COM-01, EXT-05, ESP-04 y las mediciones de los ensayos de protección) se hacen con instrumental y no necesitan nada especial del firmware.

### Limitaciones

- **DO2 y DI2 son la consola.** DO2 se presta por un tiempo fijo y después vuelve sola. Para probar DI2 hay que desconectar el TX del CP2102; mientras tanto la consola sigue imprimiendo, pero no recibe.
- **DO3 y DO4 comparten el TIM8**, así que en PWM van a la misma frecuencia (el duty sí es independiente).
- **Frecuencia en las entradas:** se mide contando flancos con EXTI, una interrupción por flanco. A 100 kHz conviene medir de a una entrada por vez.
- **FDCAN2:** `banco_can`, `can_placas`, `gateway`, `gw_fuente` e `integral` usan FDCAN2, así que el enlace CAN2 se suspende mientras corren. Las de dos placas terminan solas, al llegar a la cantidad pedida o cuando la otra placa deja de transmitir. Al terminar vuelve el enlace y la esclava manda su informe.
- **ESP32:** `esp reset` hace un pulso en EN (PE7, open-drain) y deja los dos lados a 115 200. Sirve si quedaron en velocidades distintas.

## Programas sueltos

[`Src/main.c`](Src/main.c) incluye `Apps/Pruebas.c` (o `Apps/Universal.c`). Para volver a uno de los programas anteriores, que tienen cada uno su propio `main()`, comentar esa línea y descomentar el `#include` de [`Apps/legacy/`](Apps/legacy/) que corresponda (uno solo a la vez). Esos programas no tienen consola ni enlace, y siguen manejando los /OE por su cuenta.

## Estructura

```
firmware/
├── Apps/
│   ├── Pruebas.c              Firmware del plan de pruebas: tabla de pruebas y comandos extra
│   ├── Universal.c            Firmware de bring-up: tabla de pruebas
│   ├── universal/
│   │   ├── nucleo_def.c       Tipos y declaraciones comunes a los dos firmwares
│   │   ├── nucleo.c           Arranque, manejo de pruebas, comandos y loop principal
│   │   ├── consola.c          USART1 por interrupción, buffers circulares y cprintf
│   │   ├── fdcan.c            Reloj FDCAN común y driver de FDCAN1/FDCAN2
│   │   ├── enlace.c           Protocolo de control por CAN2
│   │   ├── rol.c              Rol y parámetros en la última página de flash
│   │   └── p_*.c              Una prueba por archivo: init(), step() y deinit()
│   ├── pruebas/
│   │   ├── t_es.c             es: salidas, PWM, entradas y frecuencia
│   │   ├── t_ai.c             ai: ADC en voltios del conector
│   │   ├── t_can.c            banco_can, can_placas, gateway y gw_fuente
│   │   ├── t_esp.c            esp_uart: enlace con la ESP32
│   │   └── t_integral.c       integral: todo a la vez
│   └── legacy/                Los programas sueltos anteriores, sin cambios
├── ESP32/                     Sketches de Arduino para la ESP32 del zócalo (LED serie y ESP32_Pruebas)
├── Inc/
│   ├── ecu32_regs.h           Registros del STM32G491 (RM0440)
│   └── ecu32_placa.h          Pines de la placa y helpers de GPIO
├── Src/
│   ├── main.c                 Incluye Pruebas.c (o Universal.c, o un programa de legacy/)
│   ├── startup_stm32g491xx.S
│   ├── syscall.c, sysmem.c
├── cmake/                     Toolchain y flags (generados por STM32Cube)
├── CMakeLists.txt
├── CMakePresets.json          Presets Debug y Release
├── stm32g491xc_flash.ld       Script del linker (última página reservada para el rol)
├── .settings/                 Versiones del toolchain de STM32Cube
└── .vscode/                   Configuración de debug e IntelliSense
```

Las notas de arranque del hardware (STB de CAN, EN de LIN, etc.) están en el [README principal](../README.md#notas-de-arranque).
