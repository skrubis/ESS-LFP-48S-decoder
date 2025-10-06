from __future__ import annotations
from dataclasses import dataclass, field
from math import isnan
from typing import List


def u16be(b: bytes, o: int) -> int:
    return (b[o] << 8) | b[o + 1]


@dataclass
class ESSLFP48SDecoder:
    cell_v: List[float] = field(default_factory=lambda: [float('nan')] * 48)
    temps_c: List[float] = field(default_factory=lambda: [float('nan')] * 24)

    pack_voltage_v: float = float('nan')
    max_cell_v: float = float('nan')
    min_cell_v: float = float('nan')
    cell_delta_v: float = float('nan')
    avg_temp_c: float = float('nan')
    min_temp_c: float = float('nan')

    cell_count: int = 0
    temp_count: int = 0
    min_cell_index: int = 0
    max_cell_index: int = 0
    submodule_count: int = 0
    module_index: int = 0

    capacity: str = ""
    capacity_b1: int = 0
    capacity_b2: int = 0

    def reset(self) -> None:
        self.cell_v = [float('nan')] * 48
        self.temps_c = [float('nan')] * 24
        self.pack_voltage_v = self.max_cell_v = self.min_cell_v = float('nan')
        self.cell_delta_v = self.avg_temp_c = self.min_temp_c = float('nan')
        self.cell_count = self.temp_count = self.min_cell_index = self.max_cell_index = 0
        self.submodule_count = self.module_index = 0
        self.capacity = ""
        self.capacity_b1 = 0
        self.capacity_b2 = 0

    @staticmethod
    def _in_range(x: int, lo: int, hi: int) -> bool:
        return lo <= x <= hi

    def update_from_frame(self, can_id: int, dlc: int, data: bytes) -> bool:
        matched = False
        # Cell voltages 0x18110181 .. 0x18110C81, 4x u16 BE, scale 0.001 V
        if self._in_range(can_id, 0x18110181, 0x18110C81) and dlc == 8:
            idx = (can_id - 0x18110181) // 0x100
            base = idx * 4
            self.cell_v[base + 0] = u16be(data, 0) * 0.001
            self.cell_v[base + 1] = u16be(data, 2) * 0.001
            self.cell_v[base + 2] = u16be(data, 4) * 0.001
            self.cell_v[base + 3] = u16be(data, 6) * 0.001
            matched = True

        # Temperatures 0x18120181 .. 0x18120681, 4x u16 BE, scale 0.01 C
        if self._in_range(can_id, 0x18120181, 0x18120681) and dlc == 8:
            idx = (can_id - 0x18120181) // 0x100
            base = idx * 4
            self.temps_c[base + 0] = u16be(data, 0) * 0.01
            self.temps_c[base + 1] = u16be(data, 2) * 0.01
            self.temps_c[base + 2] = u16be(data, 4) * 0.01
            self.temps_c[base + 3] = u16be(data, 6) * 0.01
            matched = True

        # Pack summary 0x18130181: Max, Min, '4','3', PackVoltage
        if can_id == 0x18130181 and dlc >= 8:
            self.max_cell_v = u16be(data, 0) * 0.001
            self.min_cell_v = u16be(data, 2) * 0.001
            self.capacity_b1 = data[4]
            self.capacity_b2 = data[5]
            self.capacity = bytes([data[4], data[5]]).decode('ascii', errors='ignore')
            self.pack_voltage_v = u16be(data, 6) * 0.1
            matched = True

        # Counts/meta 0x18130281 (DLC 6)
        if can_id == 0x18130281 and dlc >= 6:
            self.cell_count = data[0]
            self.temp_count = data[1]
            self.min_cell_index = data[2]
            self.max_cell_index = data[3]
            self.submodule_count = data[4]
            self.module_index = data[5]
            matched = True

        # Temp/delta summary 0x18130381
        if can_id == 0x18130381 and dlc >= 8:
            self.avg_temp_c = u16be(data, 0) * 0.01
            self.min_temp_c = u16be(data, 2) * 0.01
            self.cell_delta_v = u16be(data, 4) * 0.001
            matched = True

        # 0x18130481 reserved/ignored
        if can_id == 0x18130481:
            matched = True

        return matched

    def snapshot(self) -> dict:
        return {
            'packVoltage': self.pack_voltage_v,
            'maxCellV': self.max_cell_v,
            'minCellV': self.min_cell_v,
            'cellDeltaV': self.cell_delta_v,
            'avgTempC': self.avg_temp_c,
            'minTempC': self.min_temp_c,
            'cellCount': self.cell_count,
            'tempCount': self.temp_count,
            'minCellIndex': self.min_cell_index,
            'maxCellIndex': self.max_cell_index,
            'submoduleCount': self.submodule_count,
            'moduleIndex': self.module_index,
            'capacity': self.capacity,
            'capacityAscii': self.capacity,
            'capacityBytesHex': f"0x{self.capacity_b1:02X},0x{self.capacity_b2:02X}",
            'capacityBytesDec': [self.capacity_b1, self.capacity_b2],
            'cells': self.cell_v[:],
            'temps': self.temps_c[:],
        }
