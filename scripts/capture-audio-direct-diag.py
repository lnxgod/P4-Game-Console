#!/usr/bin/env python3
"""Issue or consume the fail-closed D2.3 audio launch receipt."""

from __future__ import annotations

import argparse
import pathlib

import d23_capture_transport as transport


def parse_path(value: str) -> pathlib.Path:
    return pathlib.Path(value)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    check = commands.add_parser("check-issuable")
    check.add_argument("--state", type=parse_path, required=True)
    check.add_argument("--authorization", type=parse_path, required=True)

    preflight = commands.add_parser("preflight-host")
    preflight.add_argument("--port", required=True)

    reserve = commands.add_parser("reserve")
    reserve.add_argument("--state", type=parse_path, required=True)
    reserve.add_argument("--receipt", type=parse_path, required=True)
    reserve.add_argument("--authorization", type=parse_path, required=True)
    reserve.add_argument("--device-identity-sha256", required=True)
    reserve.add_argument("--offset", type=lambda value: int(value, 0), required=True)
    reserve.add_argument("--bytes", type=int, required=True)
    reserve.add_argument("--sha256", required=True)

    fail = commands.add_parser("fail-reservation")
    fail.add_argument("--state", type=parse_path, required=True)
    fail.add_argument("--authorization", type=parse_path, required=True)
    fail.add_argument("--reason", required=True)

    emit = commands.add_parser("emit-receipt")
    emit.add_argument("--receipt", type=parse_path, required=True)
    emit.add_argument("--state", type=parse_path, required=True)
    emit.add_argument("--port", required=True)
    emit.add_argument("--device-identity-sha256", required=True)
    emit.add_argument("--offset", type=lambda value: int(value, 0), required=True)
    emit.add_argument("--bytes", type=int, required=True)
    emit.add_argument("--sha256", required=True)
    emit.add_argument("--readback-bytes", type=int, required=True)
    emit.add_argument("--readback-sha256", required=True)
    emit.add_argument("--readback-chunks", type=int, required=True)
    emit.add_argument("--readback-max-chunk-bytes", type=int, required=True)
    emit.add_argument("--authorization", type=parse_path, required=True)
    emit.add_argument("--verifier", type=parse_path, required=True)
    emit.add_argument("--analyzer", type=parse_path, required=True)

    capture = commands.add_parser("capture")
    capture.add_argument("--receipt", type=parse_path, required=True)
    capture.add_argument("--state", type=parse_path, required=True)
    capture.add_argument("--port", required=True)
    capture.add_argument("--wav", type=parse_path, required=True)
    capture.add_argument("--serial-raw", type=parse_path, required=True)
    capture.add_argument("--timing-json", type=parse_path, required=True)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.command == "preflight-host":
        transport.preflight_host(args.port)
    elif args.command == "check-issuable":
        transport.check_issuable(args.state, args.authorization)
    elif args.command == "reserve":
        transport.reserve(args)
    elif args.command == "fail-reservation":
        transport.fail_reservation(args.state, args.authorization, args.reason)
    elif args.command == "emit-receipt":
        transport.emit_receipt(args)
    else:
        transport.capture(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
