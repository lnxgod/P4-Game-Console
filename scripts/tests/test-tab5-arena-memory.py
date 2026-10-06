#!/usr/bin/env python3
"""Reject linked arena state that steals the internal boot/DMA reserve."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('tab5_verify',Path(__file__).resolve().parents[1]/'verify-console-os-tab5.py')
verifier=importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)

class MemoryTests(unittest.TestCase):
    def test_psram_passes(self):
        verifier.verify_arena_memory('480a0000 b gc\n480b0000 b s_arena_reader\n')

    def test_internal_missing_and_out_of_range_fail(self):
        for symbols in ('4ff00000 b gc\n480b0000 b s_arena_reader\n',
                        '480a0000 b gc\n4ff00000 b s_arena_reader\n',
                        '480a0000 b gc\n4a000000 b s_arena_reader\n',''):
            with self.subTest(symbols=symbols), self.assertRaises(ValueError):
                verifier.verify_arena_memory(symbols)

if __name__=='__main__':
    unittest.main()
