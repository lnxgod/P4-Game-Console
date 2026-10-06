#!/usr/bin/env python3
"""Restrict pinned ESP-Hosted cleanup to its Tab5 SDIO slot; preserve storage."""
import argparse
import hashlib
from pathlib import Path
PIN = '797ed447ac1bb88adabb24cf1d25fc5b3bf078fec8f4c622b0f411369ffd6eef'
def generate(data):
    if hashlib.sha256(data).hexdigest() != PIN:
        raise ValueError('sdio_wrapper.c is not pinned ESP-Hosted 1.4.7')
    text=data.decode()
    if text.count('sdmmc_host_deinit();') != 2:
        raise ValueError('unexpected SDMMC cleanup topology')
    text=text.replace('sdmmc_host_deinit();','sdmmc_host_deinit_slot(context->config.slot);')
    text='#include "sdkconfig.h"\n#if !CONFIG_P4_BOARD_M5STACK_TAB5\n#error Tab5-only shared SDMMC slot ownership overlay\n#endif\n'+text
    return text.encode()
if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--input',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();output=generate(args.input.read_bytes());args.output.parent.mkdir(parents=True,exist_ok=True)
    if not args.output.exists() or args.output.read_bytes()!=output:args.output.write_bytes(output)
