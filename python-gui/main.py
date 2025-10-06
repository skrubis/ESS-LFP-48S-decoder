import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
from typing import Optional

import can

from decoder import ESSLFP48SDecoder
from collections import deque


BITRATE = 250000
DEFAULT_CHANNEL = "PCAN_USBBUS1"  # Change if you use BUS2, etc.


class BMSApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("ESS 48S BMS Monitor")
        self.geometry("1100x700")
        self.configure(bg="#0b1220")

        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TFrame", background="#0b1220")
        style.configure("TLabel", background="#0b1220", foreground="#e5e7eb")
        style.configure("Header.TLabel", font=("Segoe UI", 16, "bold"))
        style.configure("Small.TLabel", foreground="#9ca3af")
        style.configure("Good.TLabel", foreground="#34d399")
        style.configure("Bad.TLabel", foreground="#f87171")
        style.configure("Tile.TLabel", background="#0f172a")

        self.bus: Optional[can.BusABC] = None
        self.reader_thread: Optional[threading.Thread] = None
        self.stop_event = threading.Event()
        self.decoder = ESSLFP48SDecoder()

        # Debug & stats
        self.debug = True
        self.total_recv = 0
        self.total_ext = 0
        self.total_std = 0
        self.total_rtr = 0
        self.count_cells = 0
        self.count_temps = 0
        self.count_pack = 0
        self.count_counts = 0
        self.count_delta = 0
        self.count_reserved = 0
        self.last_ids = deque(maxlen=20)
        self.first_dump = 20  # dump first N frames verbosely
        self.last_rx_time = 0.0
        self._last_debug_print = time.monotonic()

        self._build_ui()
        self._update_ui_loop()

    def _build_ui(self):
        top = ttk.Frame(self)
        top.pack(fill=tk.X, padx=10, pady=10)

        ttk.Label(top, text="Interface: PCAN @ 250k", style="Header.TLabel").pack(side=tk.LEFT)

        ttk.Label(top, text="  Channel:", style="TLabel").pack(side=tk.LEFT, padx=(20, 5))
        self.channel_var = tk.StringVar(value=DEFAULT_CHANNEL)
        self.channel_entry = ttk.Entry(top, textvariable=self.channel_var, width=16)
        self.channel_entry.pack(side=tk.LEFT)

        self.connect_btn = ttk.Button(top, text="Connect", command=self.on_connect)
        self.connect_btn.pack(side=tk.LEFT, padx=10)
        self.disconnect_btn = ttk.Button(top, text="Disconnect", command=self.on_disconnect, state=tk.DISABLED)
        self.disconnect_btn.pack(side=tk.LEFT)

        self.summary = ttk.Label(self, text="Disconnected", style="Small.TLabel")
        self.summary.pack(fill=tk.X, padx=10)

        # Cells grid
        cells_card = ttk.Frame(self, padding=10)
        cells_card.pack(fill=tk.X, padx=10, pady=10)
        ttk.Label(cells_card, text="Cells (48)", style="Header.TLabel").pack(anchor="w")
        self.cells_frame = ttk.Frame(cells_card)
        self.cells_frame.pack(fill=tk.X, pady=6)

        self.cell_labels = []
        for i in range(48):
            lbl = ttk.Label(self.cells_frame, text=f"C{i+1}: -- V", style="Tile.TLabel", anchor="center", relief=tk.SOLID)
            lbl.grid(row=i // 12, column=i % 12, sticky="nsew", padx=2, pady=2)
            self.cell_labels.append(lbl)
        for c in range(12):
            self.cells_frame.columnconfigure(c, weight=1)

        # Temps grid
        temps_card = ttk.Frame(self, padding=10)
        temps_card.pack(fill=tk.X, padx=10, pady=10)
        ttk.Label(temps_card, text="Temperatures (24)", style="Header.TLabel").pack(anchor="w")
        self.temps_frame = ttk.Frame(temps_card)
        self.temps_frame.pack(fill=tk.X, pady=6)

        self.temp_labels = []
        for i in range(24):
            lbl = ttk.Label(self.temps_frame, text=f"T{i+1}: -- °C", style="Tile.TLabel", anchor="center", relief=tk.SOLID)
            lbl.grid(row=i // 12, column=i % 12, sticky="nsew", padx=2, pady=2)
            self.temp_labels.append(lbl)
        for c in range(12):
            self.temps_frame.columnconfigure(c, weight=1)

    def on_connect(self):
        if self.bus:
            return
        channel = self.channel_var.get().strip()
        try:
            # Reset stats on new connection
            self.total_recv = self.total_ext = self.total_std = self.total_rtr = 0
            self.count_cells = self.count_temps = self.count_pack = 0
            self.count_counts = self.count_delta = self.count_reserved = 0
            self.last_ids.clear()
            self.first_dump = 20
            self.last_rx_time = 0.0
            self._last_debug_print = time.monotonic()

            print(f"[GUI] Connecting to PCAN channel={channel} bitrate={BITRATE}", flush=True)
            self.bus = can.Bus(interface="pcan", channel=channel, bitrate=BITRATE)
            # Accept all frames (both std/ext); we'll filter in software
            try:
                self.bus.set_filters(None)
            except Exception:
                pass
            try:
                info = getattr(self.bus, "channel_info", None)
                if info:
                    print(f"[GUI] Connected: {info}", flush=True)
            except Exception:
                pass
            print("[GUI] Expecting extended IDs: cells 0x18110181..0x18110C81, temps 0x18120181..0x18120681, pack 0x18130181, counts 0x18130281, delta 0x18130381", flush=True)
        except Exception as e:
            messagebox.showerror("PCAN Connect Error", str(e))
            self.bus = None
            return
        self.stop_event.clear()
        self.reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self.reader_thread.start()
        self.connect_btn.configure(state=tk.DISABLED)
        self.disconnect_btn.configure(state=tk.NORMAL)
        self.summary.configure(text=f"Connected: {channel} @ {BITRATE} bps (extended frames)")

    def on_disconnect(self):
        self.stop_event.set()
        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=1.0)
        self.reader_thread = None
        if self.bus:
            try:
                self.bus.shutdown()
            except Exception:
                pass
        self.bus = None
        # Print final stats
        try:
            print(
                f"[GUI] Disconnected. Stats: recv={self.total_recv} ext={self.total_ext} std={self.total_std} rtr={self.total_rtr} "
                f"cells={self.count_cells} temps={self.count_temps} pack={self.count_pack} counts={self.count_counts} "
                f"delta={self.count_delta} reserved={self.count_reserved}",
                flush=True,
            )
            if self.last_ids:
                print(f"[GUI] Last IDs: {list(self.last_ids)}", flush=True)
        except Exception:
            pass
        self.summary.configure(text="Disconnected")
        self.connect_btn.configure(state=tk.NORMAL)
        self.disconnect_btn.configure(state=tk.DISABLED)

    def _reader_loop(self):
        while not self.stop_event.is_set():
            try:
                msg = self.bus.recv(timeout=0.1)
            except Exception:
                break
            if msg is None:
                continue
            self.total_recv += 1
            self.last_rx_time = time.time()

            # Dump first few frames verbosely
            if self.first_dump > 0:
                try:
                    data_hex = ' '.join(f"{b:02X}" for b in msg.data)
                    print(
                        f"[RX] id=0x{msg.arbitration_id:08X} dlc={msg.dlc} ext={msg.is_extended_id} rtr={msg.is_remote_frame} data={data_hex}",
                        flush=True,
                    )
                except Exception:
                    pass
                self.first_dump -= 1

            if msg.is_remote_frame:
                self.total_rtr += 1
                continue
            if not msg.is_extended_id:
                self.total_std += 1
                # We only care about extended frames for this BMS
                continue

            self.total_ext += 1

            can_id = msg.arbitration_id
            self.last_ids.append(f"0x{can_id:08X}")
            data = bytes(msg.data)

            # Categorize for stats
            matched = False
            if 0x18110181 <= can_id <= 0x18110C81 and msg.dlc == 8:
                self.count_cells += 1
                matched = True
            elif 0x18120181 <= can_id <= 0x18120681 and msg.dlc == 8:
                self.count_temps += 1
                matched = True
            elif can_id == 0x18130181:
                self.count_pack += 1
                matched = True
            elif can_id == 0x18130281:
                self.count_counts += 1
                matched = True
            elif can_id == 0x18130381:
                self.count_delta += 1
                matched = True
            elif can_id == 0x18130481:
                self.count_reserved += 1
                matched = True

            # Feed decoder
            if matched:
                self.decoder.update_from_frame(can_id, msg.dlc, data)

            # Periodic debug summary
            now = time.monotonic()
            if now - self._last_debug_print > 2.0:
                self._last_debug_print = now
                try:
                    print(
                        f"[STAT] recv={self.total_recv} ext={self.total_ext} std={self.total_std} rtr={self.total_rtr} "
                        f"cells={self.count_cells} temps={self.count_temps} pack={self.count_pack} counts={self.count_counts} "
                        f"delta={self.count_delta} reserved={self.count_reserved} last_ids={list(self.last_ids)}",
                        flush=True,
                    )
                except Exception:
                    pass

    def _update_ui_loop(self):
        snap = self.decoder.snapshot()
        self._render_summary(snap)
        self._render_cells(snap)
        self._render_temps(snap)
        self.after(500, self._update_ui_loop)

    def _render_summary(self, s: dict):
        def fmt(v, digits):
            try:
                return f"{v:.{digits}f}"
            except Exception:
                return "--"
        cap_ascii = s.get('capacityAscii') or s.get('capacity') or ''
        cap_hex = s.get('capacityBytesHex') or ''
        socStr = f" | SoC {cap_ascii}%" if isinstance(cap_ascii, str) and cap_ascii.isdigit() and len(cap_ascii) == 2 else ""
        text = (
            f"Pack {fmt(s.get('packVoltage'),1)} V | Δ {fmt(s.get('cellDeltaV'),3)} V | "
            f"Max {fmt(s.get('maxCellV'),3)} | Min {fmt(s.get('minCellV'),3)} | "
            f"AvgT {fmt(s.get('avgTempC'),2)} °C | MinT {fmt(s.get('minTempC'),2)} °C | "
            f"Cells {s.get('cellCount')} | Temps {s.get('tempCount')} | Module {s.get('moduleIndex')} | "
            f"Variant {cap_ascii} ({cap_hex}){socStr}"
        )
        self.summary.configure(text=text)

    def _render_cells(self, s: dict):
        cells = s.get('cells') or []
        for i, lbl in enumerate(self.cell_labels):
            v = cells[i] if i < len(cells) else None
            if v is None or (isinstance(v, float) and (v != v)):
                lbl.configure(text=f"C{i+1}: -- V", foreground="#9ca3af")
            else:
                color = "#34d399" if 3.0 <= v <= 3.7 else "#f87171"
                lbl.configure(text=f"C{i+1}: {v:.3f} V", foreground=color)

    def _render_temps(self, s: dict):
        temps = s.get('temps') or []
        for i, lbl in enumerate(self.temp_labels):
            t = temps[i] if i < len(temps) else None
            if t is None or (isinstance(t, float) and (t != t)):
                lbl.configure(text=f"T{i+1}: -- °C", foreground="#9ca3af")
            else:
                color = "#34d399" if -10 <= t <= 55 else "#f87171"
                lbl.configure(text=f"T{i+1}: {t:.2f} °C", foreground=color)


if __name__ == "__main__":
    app = BMSApp()
    app.mainloop()
