"""H1 CH343 serial transport for the Mac realm hub."""

from __future__ import annotations

import threading
import time

from tools.p4_realm_hub.hub import RealmHubSession
from tools.p4_realm_hub.p4mp import StreamDecoder
from tools.p4_realm_hub.store import RealmStore


class SerialRealmLink:
    def __init__(
        self,
        profile: str,
        port: str,
        store: RealmStore,
        *,
        baudrate: int = 115200,
    ) -> None:
        try:
            import serial
        except ImportError as error:
            raise RuntimeError("pyserial is required for H1 realm sync") from error
        self._serial_module = serial
        self.profile = profile
        self.port = port
        self.baudrate = baudrate
        self.decoder = StreamDecoder()
        self.stop_event = threading.Event()
        self.device = None
        self.session = RealmHubSession(profile, store, self.send)

    def open(self) -> None:
        device = self._serial_module.Serial(
            port=None,
            baudrate=self.baudrate,
            timeout=0.05,
            write_timeout=1.0,
            exclusive=True,
        )
        device.dtr = False
        device.rts = False
        device.port = self.port
        device.open()
        if device.dtr or device.rts:
            device.close()
            raise RuntimeError("H1 reset controls became active")
        self.device = device

    def send(self, datagram: bytes) -> None:
        if self.device is None:
            raise RuntimeError("serial realm link is closed")
        written = self.device.write(datagram)
        if written != len(datagram):
            raise RuntimeError("short H1 realm write")
        self.device.flush()

    def run(self) -> None:
        while not self.stop_event.is_set():
            try:
                self.open()
                print(
                    f"P4_REALM_HUB READY transport=h1 profile={self.profile} "
                    f"port={self.port} baud={self.baudrate}",
                    flush=True,
                )
                while not self.stop_event.is_set():
                    assert self.device is not None
                    waiting = min(int(self.device.in_waiting), 4096)
                    data = self.device.read(waiting if waiting else 1)
                    for frame in self.decoder.feed(data):
                        self.session.receive(frame)
                    self.session.tick()
                    if not data:
                        time.sleep(0.005)
            except (
                self._serial_module.SerialException,
                OSError,
                RuntimeError,
            ) as error:
                print(
                    f"P4_REALM_HUB RETRY transport=h1 profile={self.profile} "
                    f"error={error}",
                    flush=True,
                )
            finally:
                self.session.transport_disconnected()
                if self.device is not None:
                    self.device.close()
                    self.device = None
            self.stop_event.wait(1.0)

    def stop(self) -> None:
        self.stop_event.set()
