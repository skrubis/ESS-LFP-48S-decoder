# ESS LFP 48S BMS Decoder

Decoder, Arduino library, and Python GUI for an industrial ESS module comprised of 48S LFP packs (approx. 43 kWh, 280 Ah). The BMS broadcasts on a shared 250 kbit/s extended (29‑bit) CAN bus.

- Repository paths
  - `dbc/ESS-LFP-48S-can.dbc`
  - `can-capture/bms-can-capture.txt` (PCAN-View trace)
  - `arduino-library/ESS_LFP_48S/` (Arduino library with examples for ESP32 + TWAI)
  - `python-gui/` (Python GUI for PCAN-USB on Windows)

## 1) DBC vs CAN capture sanity check

Observed IDs and payloads in `can-capture/bms-can-capture.txt` match the DBC in `dbc/ESS-LFP-48S-can.dbc`:

- **Cell voltages (`BMS81_CellVoltages_01..12`)**
  - IDs: `0x18110181 .. 0x18110C81` (step `0x100`), DLC 8.
  - Payload: 4× `u16` big-endian, scale 0.001 V -> 48 cells total.
  - Example: `18110181 8 0C E7 0C EA 0C EA 0C EA` → [3.303, 3.306, 3.306, 3.306] V.

- **Temperatures (`BMS81_Temps_01..06`)**
  - IDs: `0x18120181 .. 0x18120681` (step `0x100`), DLC 8.
  - Payload: 4× `u16` big-endian, scale 0.01 °C -> 24 temps total.
  - Example: `18120181 8 03 27 03 29 03 28 03 29` → [8.07, 8.09, 8.08, 8.09] °C.

- **Pack summary (`BMS81_PackSummary`)**
  - ID: `0x18130181`, DLC 8.
  - Payload: `MaxCell(u16,0.001V) MinCell(u16,0.001V) '4' '3' PackVoltage(u16,0.1V)`.
  - Example: `18130181 8 0C EC 0C E2 34 33 06 32` → Max 3.308 V, Min 3.298 V, Capacity "43", Pack 158.6 V.

- **Counts / meta (`BMS81_CountsMeta`)**
  - ID: `0x18130281`, DLC 6.
  - Payload: `CellCount TempCount MinCellIdx MaxCellIdx SubmoduleCount ModuleIndex`.
  - Example: `18130281 6 30 18 08 0D 0C 01` → 48 cells, 24 temps, min idx 8, max idx 13, submodules 12, module index 1.

- **Temperature + cell delta summary (`BMS81_TempDeltaSummary`)**
  - ID: `0x18130381`, DLC 8.
  - Payload: `AvgTemp(u16,0.01C) MinTemp(u16,0.01C) CellDelta(u16,0.001V) Unknown(u16)`.
  - Example: `18130381 8 03 35 03 27 00 0A 00 0E` → Avg 8.21 °C, Min 8.07 °C, Δ 0.010 V.

- **Reserved/Unknown (`BMS81_Reserved`)**
  - ID: `0x18130481`, DLC 8, often zeros.

- Additional periodic frames like `0x180101F4`, `0x1801F401`, ... are present in the capture and likely originate from nodes `HOST_F4` / `MASTER_01` (per DBC node list). These are not needed for values above and are ignored by this decoder.

- Bus speed confirmed 250 kbit/s; frames are extended (29-bit).

- Ballpark values requested by user match capture:
  - Pack voltage ≈ 158.6–158.8 V (`PackVoltage_V`).
  - Individual cells ≈ 3.30–3.31 V.
  - Temperatures ≈ 8–10 °C.

## 2) Arduino library: ESP32 + TWAI

Path: `arduino-library/ESS_LFP_48S/`

- Library class `ESS_LFP_48S` (`src/ESS_LFP_48S.h/.cpp`):
  - `updateFromFrame(id, dlc, data)` parses frames defined above.
  - Accessors for 48 cell voltages, 24 temperatures, pack voltage, min/max, delta, and meta.
  - `toJson(String&)` builds a compact JSON snapshot for web UIs.

- Examples:
  - `examples/ESP32_TWAI_Serial/ESP32_TWAI_Serial.ino`
    - Prints pack summary + 48 cells + 24 temps once per second to Serial.
  - `examples/ESP32_TWAI_Web/ESP32_TWAI_Web.ino`
    - Simple WiFi web server (ESP32) serving an HTML dashboard and `/api` JSON endpoint.

- Wiring (typical):
  - Transceiver (e.g., SN65HVD230) -> ESP32.
  - Defaults in examples: `TX=GPIO5`, `RX=GPIO4`.
  - Define `TWAI_TX_PIN` / `TWAI_RX_PIN` at compile time to override.

- Build notes:
  - Board: any ESP32 with Arduino core supporting `driver/twai.h`.
  - Bitrate fixed to 250 kbit/s in examples.
  - Only extended frames are consumed; standard/RTR frames ignored.

## 3) Python GUI (Windows, PCAN-USB)

Path: `python-gui/`

- Install dependencies:

```bash
python -m venv .venv
.venv\Scripts\pip install -r python-gui/requirements.txt
```

- PCAN driver/runtime:
  - Install PEAK PCAN-Basic for Windows (provides `PCANBasic.dll`).
  - Use PCAN-View to verify the adapter, then close it before running this GUI.

- Run GUI:

```bash
.venv\Scripts\python python-gui/main.py
```

- In the app:
  - Set Channel to `PCAN_USBBUS1` (or `PCAN_USBBUS2` as appropriate).
  - Click Connect. The app shows 48 cells, 24 temps, and pack summary live.

- Implementation details:
  - `decoder.py` mirrors the Arduino parser: same IDs, endianness, scaling.
  - Uses `python-can` with `bustype=pcan`, `bitrate=250000`.

## 4) Known mapping summary

- Cells (48): `0x18110181 + n*0x100`, `n=0..11`, 4 cells per frame.
- Temps (24): `0x18120181 + n*0x100`, `n=0..5`, 4 temps per frame.
- Pack summary: `0x18130181` → Max/Min cell V (0.001V), ASCII capacity (e.g., "43"), Pack V (0.1V).
- Counts/meta: `0x18130281` → CellCount, TempCount, Min/Max cell indices, SubmoduleCount, ModuleIndex.
- Temp+Delta summary: `0x18130381` → AvgTemp(0.01C), MinTemp(0.01C), CellDelta(0.001V), Unknown(u16).
- Reserved: `0x18130481` (ignored).

## Notes and open items

- The `SubmoduleCount` reported as 12 likely indicates 12 groups of 4 cells in the wire format (12 frames × 4 cells), not physical submodules. The physical pack uses 4×12S; `ModuleIndex` appears to enumerate the big module on the shared bus.
- Fields `BMS81_Reserved` and the last `u16` in `BMS81_TempDeltaSummary` remain unknown.
- Additional frames with IDs like `0x180101F4`, `0x1801F401`, `0x1811F401`, etc., are present and likely related to `HOST_F4` / `MASTER_01`; they are currently ignored.

## License

MIT (add your preferred license if different).
