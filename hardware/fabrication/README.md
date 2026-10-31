# Archivos de fabricación (JLCPCB)

Subí acá **exactamente lo que se mandó a fabricar**, versionado por revisión:

```
fabrication/
└── RevA/
    ├── ECU32_RevA_Gerber.zip      Gerbers + NC Drill
    ├── ECU32_RevA_PickPlace.csv   (si hubo armado SMT)
    └── order-notes.md             Fecha, espesor, color, acabado, cantidad
```

**Configuración usada:**

- **Gerber:** Fabrication Outputs → Gerber Files, en mm.
- **NC Drill:** Excellon, en mm, formato 4:4.
- **Pick & Place:** Assembly Outputs → Generate Pick and Place Files (CSV).
- Antes de armar el zip, sacá `Status Report.Txt` y el reporte de DRC.
