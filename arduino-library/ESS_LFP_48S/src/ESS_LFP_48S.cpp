#include "ESS_LFP_48S.h"

ESS_LFP_48S::ESS_LFP_48S() { reset(); }

void ESS_LFP_48S::reset() {
  for (int i = 0; i < 48; ++i) _cellV[i] = NAN;
  for (int i = 0; i < 24; ++i) _tempC[i] = NAN;
  _packVoltage = _maxCellV = _minCellV = _cellDeltaV = _avgTempC = _minTempC = NAN;
  _cellCount = _tempCount = _minCellIndex = _maxCellIndex = _submoduleCount = _moduleIndex = 0;
  _capacityChar1 = _capacityChar2 = 0;
}

static inline bool in_range(uint32_t x, uint32_t lo, uint32_t hi) { return x >= lo && x <= hi; }

bool ESS_LFP_48S::updateFromFrame(uint32_t id, uint8_t dlc, const uint8_t *d) {
  bool matched = false;

  // Cell voltages: 0x18110181 .. 0x18110C81, step 0x100, 4 cells per frame, 16-bit big-endian, scale 0.001 V
  if (in_range(id, 0x18110181, 0x18110C81) && dlc == 8) {
    uint32_t idx = (id - 0x18110181) / 0x100; // 0..11
    uint32_t base = idx * 4;                  // 0..44
    _cellV[base + 0] = (float)u16be(&d[0]) * 0.001f;
    _cellV[base + 1] = (float)u16be(&d[2]) * 0.001f;
    _cellV[base + 2] = (float)u16be(&d[4]) * 0.001f;
    _cellV[base + 3] = (float)u16be(&d[6]) * 0.001f;
    matched = true;
  }

  // Temperatures: 0x18120181 .. 0x18120681, step 0x100, 4 temps per frame, 16-bit BE, scale 0.01 C
  if (in_range(id, 0x18120181, 0x18120681) && dlc == 8) {
    uint32_t idx = (id - 0x18120181) / 0x100; // 0..5
    uint32_t base = idx * 4;                  // 0..20
    _tempC[base + 0] = (float)u16be(&d[0]) * 0.01f;
    _tempC[base + 1] = (float)u16be(&d[2]) * 0.01f;
    _tempC[base + 2] = (float)u16be(&d[4]) * 0.01f;
    _tempC[base + 3] = (float)u16be(&d[6]) * 0.01f;
    matched = true;
  }

  // Pack summary: 0x18130181 (MaxCell_V, MinCell_V, '4','3', PackVoltage_V)
  if (id == 0x18130181 && dlc >= 8) {
    _maxCellV = (float)u16be(&d[0]) * 0.001f;
    _minCellV = (float)u16be(&d[2]) * 0.001f;
    _capacityChar1 = d[4];
    _capacityChar2 = d[5];
    _packVoltage = (float)u16be(&d[6]) * 0.1f;
    matched = true;
  }

  // Counts & meta: 0x18130281 (DLC 6)
  if (id == 0x18130281 && dlc >= 6) {
    _cellCount = d[0];
    _tempCount = d[1];
    _minCellIndex = d[2];
    _maxCellIndex = d[3];
    _submoduleCount = d[4];
    _moduleIndex = d[5];
    matched = true;
  }

  // Temp/Delta summary: 0x18130381
  if (id == 0x18130381 && dlc >= 8) {
    _avgTempC = (float)u16be(&d[0]) * 0.01f;
    _minTempC = (float)u16be(&d[2]) * 0.01f;
    _cellDeltaV = (float)u16be(&d[4]) * 0.001f;
    // d[6..7] unknown
    matched = true;
  }

  // Reserved: 0x18130481 (ignored)
  if (id == 0x18130481) {
    matched = true; // known but ignored content
  }

  return matched;
}

String ESS_LFP_48S::capacityString() const {
  char buf[3];
  buf[0] = (char)_capacityChar1;
  buf[1] = (char)_capacityChar2;
  buf[2] = '\0';
  return String(buf);
}

void ESS_LFP_48S::toJson(String &out) const {
  out.reserve(2048);
  out = "{";
  out += "\"packVoltage\":" + String(packVoltage(), 1) + ",";
  out += "\"maxCellV\":" + String(maxCellV(), 3) + ",";
  out += "\"minCellV\":" + String(minCellV(), 3) + ",";
  out += "\"cellDeltaV\":" + String(cellDeltaV(), 3) + ",";
  out += "\"avgTempC\":" + String(avgTempC(), 2) + ",";
  out += "\"minTempC\":" + String(minTempC(), 2) + ",";
  out += "\"cellCount\":" + String(cellCount()) + ",";
  out += "\"tempCount\":" + String(tempCount()) + ",";
  out += "\"minCellIndex\":" + String(minCellIndex()) + ",";
  out += "\"maxCellIndex\":" + String(maxCellIndex()) + ",";
  out += "\"submoduleCount\":" + String(submoduleCount()) + ",";
  out += "\"moduleIndex\":" + String(moduleIndex()) + ",";
  out += "\"capacity\":\"" + capacityString() + "\",";
  // Also expose raw capacity bytes for diagnostics
  out += "\"capacityAscii\":\"" + capacityString() + "\",";
  char hexbuf[16];
  snprintf(hexbuf, sizeof(hexbuf), "0x%02X,0x%02X", _capacityChar1, _capacityChar2);
  out += "\"capacityBytesHex\":\"" + String(hexbuf) + "\",";
  out += "\"capacityBytesDec\":[" + String(_capacityChar1) + "," + String(_capacityChar2) + "],";
  out += "\"cells\":[";
  for (int i = 0; i < 48; ++i) {
    if (i) out += ",";
    out += isnan(_cellV[i]) ? "null" : String(_cellV[i], 3);
  }
  out += "],\"temps\":[";
  for (int i = 0; i < 24; ++i) {
    if (i) out += ",";
    out += isnan(_tempC[i]) ? "null" : String(_tempC[i], 2);
  }
  out += "]}";
}
