# Bill of Materials

| Archivo | Contenido |
|---|---|
| [`ECU32_BOM.csv`](ECU32_BOM.csv) | BOM agrupado por parte y valor (GitHub lo muestra como tabla) |
| [`ECU32_BOM.xlsx`](ECU32_BOM.xlsx) | Lo mismo en Excel, con filtros |

**198 componentes en 71 líneas.** Columnas: ítem, cantidad, designadores, valor, parte (MPN), fabricante, descripción, footprint y notas.

## Cómo se generó

A partir del BOM que exporta Altium (Reports → Bill of Materials) y de los parámetros de cada componente en los `.SchDoc`. El BOM de Altium agrupa por el nombre de la librería, lo que da resultados incorrectos en algunos casos, así que el valor se toma del parámetro *Value*, *Capacitance* o *Inductance* de cada componente:

- **Resistencias:** en la librería todas usan el MPN `MFR-25FBF52-47R5` (47,5 Ω). Acá se agrupan por su valor real; son de la serie Yageo MFR-25, 1 %, 1/4 W.
- **Capacitores C0G radiales (Murata RCE5C1H):** en la librería todos usan el MPN del de 10 pF (`RCE5C1H100J0A2H03B`), pero en el esquemático hay de 10 pF, 22 pF, 4,7 nF, 10 nF y 100 nF. Hay que comprar el MPN de cada valor.
- **Capacitores cerámicos genéricos:** no tienen MPN en la librería; se listan por valor.

## Pendientes

- F1: el MPN del portafusible se exportó como número (`9.52017E+14`).
- `J?`: hay un header de 2×10 sin designador asignado en el esquemático.
- Faltan los links de DigiKey.
