#pragma once
#include <Arduino.h>

class ESS_LFP_48S {
public:
  ESS_LFP_48S();
  void reset();

  // Parse a single CAN frame. Returns true if the frame matched the known map.
  bool updateFromFrame(uint32_t id, uint8_t dlc, const uint8_t *data);

  // Accessors
  inline float getCellV(uint8_t idx) const { return (idx < 48) ? _cellV[idx] : NAN; }
  inline const float *cells() const { return _cellV; }
  inline float getTempC(uint8_t idx) const { return (idx < 24) ? _tempC[idx] : NAN; }
  inline const float *temps() const { return _tempC; }

  inline float packVoltage() const { return _packVoltage; }
  inline float maxCellV() const { return _maxCellV; }
  inline float minCellV() const { return _minCellV; }
  inline float cellDeltaV() const { return _cellDeltaV; }
  inline float avgTempC() const { return _avgTempC; }
  inline float minTempC() const { return _minTempC; }

  inline uint8_t cellCount() const { return _cellCount; }
  inline uint8_t tempCount() const { return _tempCount; }
  inline uint8_t minCellIndex() const { return _minCellIndex; }
  inline uint8_t maxCellIndex() const { return _maxCellIndex; }
  inline uint8_t submoduleCount() const { return _submoduleCount; }
  inline uint8_t moduleIndex() const { return _moduleIndex; }

  // Two ASCII-tag bytes observed as "43" in logs; may vary per device/variant.
  String capacityString() const;
  inline uint8_t capacityB1() const { return _capacityChar1; }
  inline uint8_t capacityB2() const { return _capacityChar2; }

  // Render a compact JSON snapshot into out.
  void toJson(String &out) const;

private:
  static inline uint16_t u16be(const uint8_t *p) { return (uint16_t(p[0]) << 8) | uint16_t(p[1]); }

  float _cellV[48];
  float _tempC[24];

  float _packVoltage = NAN;
  float _maxCellV = NAN;
  float _minCellV = NAN;
  float _cellDeltaV = NAN;
  float _avgTempC = NAN;
  float _minTempC = NAN;

  uint8_t _cellCount = 0;
  uint8_t _tempCount = 0;
  uint8_t _minCellIndex = 0;
  uint8_t _maxCellIndex = 0;
  uint8_t _submoduleCount = 0;
  uint8_t _moduleIndex = 0;

  uint8_t _capacityChar1 = 0;
  uint8_t _capacityChar2 = 0;
};
