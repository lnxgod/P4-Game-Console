"""Optional macOS BLE central transport for a Console OS hosted room."""

from __future__ import annotations

import asyncio
import struct

from tools.p4_realm_hub import p4mp
from tools.p4_realm_hub.hub import RealmHubSession
from tools.p4_realm_hub.store import RealmStore


SERVICE_UUID = "7b0d9f20-6f44-4a0d-9c9e-50344d500001"
CHARACTERISTIC_UUID = "7b0d9f20-6f44-4a0d-9c9e-50344d500002"
FRAGMENT_MAGIC = b"P4B"
FRAGMENT_VERSION = 1
FRAGMENT_HEADER_BYTES = 12
FRAGMENT_START = 1
FRAGMENT_END = 2


class BleReassembler:
    def __init__(self) -> None:
        self.frame_id = 0
        self.total = 0
        self.data = bytearray()

    def consume(self, fragment: bytes) -> bytes | None:
        if not FRAGMENT_HEADER_BYTES < len(fragment) <= 244:
            self.reset()
            return None
        magic, version, flags, reserved, frame_id, offset, total = struct.unpack_from(
            "<3sBBBHHH", fragment
        )
        if (
            magic != FRAGMENT_MAGIC
            or version != FRAGMENT_VERSION
            or reserved != 0
            or flags & ~(FRAGMENT_START | FRAGMENT_END)
            or frame_id == 0
            or not p4mp.HEADER_BYTES + p4mp.TRAILER_BYTES
            <= total
            <= p4mp.MAX_DATAGRAM_BYTES
        ):
            self.reset()
            return None
        payload = fragment[FRAGMENT_HEADER_BYTES:]
        if flags & FRAGMENT_START:
            if offset != 0:
                self.reset()
                return None
            self.frame_id = frame_id
            self.total = total
            self.data = bytearray()
        if frame_id != self.frame_id or total != self.total or offset != len(self.data):
            self.reset()
            return None
        self.data.extend(payload)
        if len(self.data) > total:
            self.reset()
            return None
        if not flags & FRAGMENT_END:
            return None
        if len(self.data) != total:
            self.reset()
            return None
        datagram = bytes(self.data)
        self.reset()
        p4mp.decode_packet(datagram)
        return datagram

    def reset(self) -> None:
        self.frame_id = 0
        self.total = 0
        self.data = bytearray()


def fragment_datagram(datagram: bytes, frame_id: int, capacity: int) -> list[bytes]:
    p4mp.decode_packet(datagram)
    capacity = min(capacity, 244)
    if frame_id == 0 or capacity <= FRAGMENT_HEADER_BYTES:
        raise ValueError("invalid BLE fragment configuration")
    fragments: list[bytes] = []
    offset = 0
    while offset < len(datagram):
        payload_bytes = min(capacity - FRAGMENT_HEADER_BYTES, len(datagram) - offset)
        next_offset = offset + payload_bytes
        flags = (FRAGMENT_START if offset == 0 else 0) | (
            FRAGMENT_END if next_offset == len(datagram) else 0
        )
        fragments.append(
            struct.pack(
                "<3sBBBHHH",
                FRAGMENT_MAGIC,
                FRAGMENT_VERSION,
                flags,
                0,
                frame_id,
                offset,
                len(datagram),
            )
            + datagram[offset:next_offset]
        )
        offset = next_offset
    return fragments


class BleRealmLink:
    def __init__(
        self,
        profile: str,
        store: RealmStore,
        *,
        room_session_id: int | None = None,
    ) -> None:
        self.profile = profile
        self.store = store
        self.room_session_id = room_session_id
        self.outgoing: asyncio.Queue[bytes] = asyncio.Queue()
        self.session = RealmHubSession(profile, store, self.outgoing.put_nowait)
        self.reassembler = BleReassembler()
        self.next_frame_id = 1

    async def run(self) -> None:
        try:
            from bleak import BleakClient, BleakScanner
        except ImportError as error:
            raise RuntimeError("bleak is required for BLE realm sync") from error

        while True:
            try:
                await self._run_once(BleakClient, BleakScanner)
            except Exception as error:
                self._transport_disconnected()
                print(
                    f"P4_REALM_HUB RETRY transport=ble profile={self.profile} "
                    f"error={error}",
                    flush=True,
                )
                await asyncio.sleep(1.0)

    async def _run_once(self, BleakClient, BleakScanner) -> None:

        def room_filter(device, advertisement) -> bool:
            data = next(
                (
                    value
                    for uuid, value in advertisement.service_data.items()
                    if str(uuid).lower() == SERVICE_UUID
                ),
                None,
            )
            if data is None or len(data) != 10:
                return False
            version, flags, session_id, game_token, present, capacity = (
                struct.unpack("<BBIHBB", data)
            )
            return (
                version == 1
                and flags == 1
                and session_id != 0
                and game_token != 0
                and present == 1
                and capacity == 2
                and (
                    self.room_session_id is None
                    or session_id == self.room_session_id
                )
            )

        print(f"P4_REALM_HUB SCANNING transport=ble profile={self.profile}", flush=True)
        device = await BleakScanner.find_device_by_filter(room_filter, timeout=30.0)
        if device is None:
            raise RuntimeError("no matching P4MP BLE room found")
        async with BleakClient(device) as client:
            characteristic = client.services.get_characteristic(CHARACTERISTIC_UUID)
            if characteristic is None:
                raise RuntimeError("P4MP BLE characteristic is missing")
            capacity = max(
                FRAGMENT_HEADER_BYTES + 1,
                min(characteristic.max_write_without_response_size, 244),
            )

            def notification(_characteristic, value: bytearray) -> None:
                datagram = self.reassembler.consume(bytes(value))
                if datagram is not None:
                    self.session.receive(datagram)

            await client.start_notify(characteristic, notification)
            discover = p4mp.encode_packet(p4mp.DISCOVER, 0, 0, 1)
            await self._write_datagram(client, characteristic, discover, capacity)
            print(
                f"P4_REALM_HUB READY transport=ble profile={self.profile} "
                f"device={device.name or 'P4 console'}",
                flush=True,
            )
            while client.is_connected:
                try:
                    datagram = await asyncio.wait_for(self.outgoing.get(), timeout=0.05)
                except TimeoutError:
                    self.session.tick()
                    continue
                await self._write_datagram(client, characteristic, datagram, capacity)
        self._transport_disconnected()

    def _transport_disconnected(self) -> None:
        self.session.transport_disconnected()
        self.reassembler.reset()
        while not self.outgoing.empty():
            self.outgoing.get_nowait()

    async def _write_datagram(
        self, client, characteristic, datagram: bytes, capacity: int
    ) -> None:
        frame_id = self.next_frame_id
        self.next_frame_id = self.next_frame_id % 0xFFFF + 1
        for fragment in fragment_datagram(datagram, frame_id, capacity):
            await client.write_gatt_char(characteristic, fragment, response=True)
