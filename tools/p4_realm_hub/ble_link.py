"""macOS CoreBluetooth peripheral transport for a backend-hosted room."""

from __future__ import annotations

import struct
from collections import deque

from tools.p4_realm_hub import p4mp
from tools.p4_realm_hub.hub import RealmHubSession
from tools.p4_realm_hub.store import RealmStore


SERVICE_UUID = "7b0d9f20-6f44-4a0d-9c9e-50344d500001"
CHARACTERISTIC_UUID = "7b0d9f20-6f44-4a0d-9c9e-50344d500002"
UUID_ONLY_SESSION_ID = 0x4C4F5244
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
        offer: p4mp.Offer,
    ) -> None:
        self.profile = profile
        self.store = store
        self.offer = offer
        self.session = RealmHubSession(
            profile,
            store,
            self._queue_datagram,
            offer,
            session_id=UUID_ONLY_SESSION_ID,
            log_event=lambda message: print(
                f"P4_REALM_HUB {message} transport=ble", flush=True
            ),
        )
        self.reassembler = BleReassembler()
        self.next_frame_id = 1
        self.pending_fragments: deque[bytes] = deque()
        self.manager = None
        self.characteristic = None
        self.central = None
        self.delegate = None
        self.ready = False

    def run(self, stop_event) -> None:
        try:
            import CoreBluetooth
            import CoreFoundation
            import Foundation
        except ImportError as error:
            raise RuntimeError("PyObjC CoreBluetooth is required for BLE hosting") from error

        link = self

        class PeripheralDelegate(Foundation.NSObject):
            def peripheralManagerDidUpdateState_(delegate_self, manager):
                link._state_updated(CoreBluetooth, Foundation, manager)

            def peripheralManager_didAddService_error_(
                delegate_self, manager, _service, error
            ):
                if error is not None:
                    link._report_error("add-service", error)
                    return
                manager.startAdvertising_(
                    {
                        CoreBluetooth.CBAdvertisementDataServiceUUIDsKey: [
                            CoreBluetooth.CBUUID.UUIDWithString_(SERVICE_UUID)
                        ]
                    }
                )

            def peripheralManagerDidStartAdvertising_error_(
                delegate_self, _manager, error
            ):
                if error is not None:
                    link._report_error("advertise", error)
                    return
                print(
                    f"P4_REALM_HUB HOSTING transport=ble profile={link.profile} "
                    f"session={UUID_ONLY_SESSION_ID:08x}",
                    flush=True,
                )

            def peripheralManager_central_didSubscribeToCharacteristic_(
                delegate_self, _manager, central, characteristic
            ):
                link._subscribed(central, characteristic)

            def peripheralManager_central_didUnsubscribeFromCharacteristic_(
                delegate_self, _manager, central, _characteristic
            ):
                link._unsubscribed(central)

            def peripheralManager_didReceiveWriteRequests_(
                delegate_self, manager, requests
            ):
                link._receive_requests(CoreBluetooth, manager, requests)

            def peripheralManagerIsReadyToUpdateSubscribers_(
                delegate_self, _manager
            ):
                link._drain(Foundation)

        self.delegate = PeripheralDelegate.alloc().init()
        self.manager = CoreBluetooth.CBPeripheralManager.alloc().initWithDelegate_queue_options_(
            self.delegate, None, None
        )
        try:
            while not stop_event.is_set():
                CoreFoundation.CFRunLoopRunInMode(
                    CoreFoundation.kCFRunLoopDefaultMode, 0.05, False
                )
                self.session.tick()
                self._drain(Foundation)
        finally:
            self.manager.stopAdvertising()
            self.manager.removeAllServices()
            self._transport_disconnected()

    def _state_updated(self, CoreBluetooth, Foundation, manager) -> None:
        if manager.state() != CoreBluetooth.CBManagerStatePoweredOn:
            self.ready = False
            return
        service_uuid = CoreBluetooth.CBUUID.UUIDWithString_(SERVICE_UUID)
        characteristic_uuid = CoreBluetooth.CBUUID.UUIDWithString_(
            CHARACTERISTIC_UUID
        )
        properties = (
            CoreBluetooth.CBCharacteristicPropertyWrite
            | CoreBluetooth.CBCharacteristicPropertyWriteWithoutResponse
            | CoreBluetooth.CBCharacteristicPropertyNotify
        )
        permissions = (
            CoreBluetooth.CBAttributePermissionsWriteable
            | CoreBluetooth.CBAttributePermissionsWriteEncryptionRequired
        )
        self.characteristic = (
            CoreBluetooth.CBMutableCharacteristic.alloc()
            .initWithType_properties_value_permissions_(
                characteristic_uuid, properties, None, permissions
            )
        )
        service = CoreBluetooth.CBMutableService.alloc().initWithType_primary_(
            service_uuid, True
        )
        service.setCharacteristics_([self.characteristic])
        manager.removeAllServices()
        manager.addService_(service)
        self.ready = True

    def _report_error(self, stage: str, error) -> None:
        print(
            f"P4_REALM_HUB ERROR transport=ble profile={self.profile} "
            f"stage={stage} error={error}",
            flush=True,
        )

    def _same_central(self, first, second) -> bool:
        return (
            first is not None
            and second is not None
            and first.identifier() == second.identifier()
        )

    def _subscribed(self, central, characteristic) -> None:
        if characteristic != self.characteristic:
            return
        if self.central is not None and not self._same_central(self.central, central):
            return
        self.central = central
        self._drain()

    def _unsubscribed(self, central) -> None:
        if self._same_central(self.central, central):
            self.central = None
            self._transport_disconnected()

    def _receive_requests(self, CoreBluetooth, manager, requests) -> None:
        for request in requests:
            result = CoreBluetooth.CBATTErrorWriteNotPermitted
            if (
                request.characteristic() == self.characteristic
                and (
                    self.central is None
                    or self._same_central(self.central, request.central())
                )
            ):
                try:
                    datagram = self.reassembler.consume(bytes(request.value()))
                    if datagram is not None:
                        self.session.receive(datagram)
                    result = CoreBluetooth.CBATTErrorSuccess
                except (TypeError, ValueError):
                    result = CoreBluetooth.CBATTErrorInvalidAttributeValueLength
            manager.respondToRequest_withResult_(request, result)

    def _queue_datagram(self, datagram: bytes) -> None:
        capacity = 20
        if self.central is not None:
            capacity = max(
                FRAGMENT_HEADER_BYTES + 1,
                min(int(self.central.maximumUpdateValueLength()), 244),
            )
        frame_id = self.next_frame_id
        self.next_frame_id = self.next_frame_id % 0xFFFF + 1
        self.pending_fragments.extend(
            fragment_datagram(datagram, frame_id, capacity)
        )
        self._drain()

    def _drain(self, Foundation=None) -> None:
        if (
            self.manager is None
            or self.characteristic is None
            or self.central is None
        ):
            return
        if Foundation is None:
            import Foundation
        while self.pending_fragments:
            fragment = self.pending_fragments[0]
            value = Foundation.NSData.dataWithBytes_length_(
                fragment, len(fragment)
            )
            if not self.manager.updateValue_forCharacteristic_onSubscribedCentrals_(
                value, self.characteristic, [self.central]
            ):
                return
            self.pending_fragments.popleft()

    def _transport_disconnected(self) -> None:
        self.session.transport_disconnected()
        self.reassembler.reset()
        self.pending_fragments.clear()
