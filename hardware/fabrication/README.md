# Archivos de fabricación (JLCPCB)

[`ECU32_Gerber.zip`](ECU32_Gerber.zip) es el paquete para fabricar la placa: se sube tal cual a JLCPCB. Sale de `PCB.PcbDoc` y es el mismo que se mandó a fabricar en octubre de 2026.

| Archivo | Capa |
|---|---|
| `PCB.GTL` / `PCB.GBL` | Cobre top / bottom |
| `PCB.GTS` / `PCB.GBS` | Máscara antisoldante top / bottom |
| `PCB.GTO` / `PCB.GBO` | Serigrafía top / bottom |
| `PCB.GTP` | Pasta top (sólo se usa si se pide stencil) |
| `PCB.GM` | Contorno de la placa |
| `PCB.TXT` | NC Drill: 730 agujeros metalizados y 4 de montaje de 5 mm sin metalizar |

Espesor, color, acabado y cantidad no van en los archivos: se eligen en el formulario de JLCPCB.

**No se incluyen**, porque el fabricante no los usa:

- `PCB.GBP` (pasta bottom): está vacía, no hay componentes SMD abajo.
- `PCB.GD1` / `PCB.GG1` (dibujo y guía de perforación): son documentación; los agujeros salen de `PCB.TXT`.
- Reportes de Altium (`.DRR`, `.LDP`, `.REP`, `.EXTREP`, `.apr`, DRC, `Status Report.Txt`).

No incluye Pick & Place (no se generó desde Altium). La BOM con los componentes está en [`../bom/`](../bom/).

## Cómo regenerarlo desde Altium

- **Gerber:** Fabrication Outputs → Gerber Files, en mm, formato 4:4.
- **NC Drill:** Fabrication Outputs → NC Drill Files, Excellon, en mm, formato 4:4.
- Armar el zip sólo con los archivos de la tabla de arriba.
