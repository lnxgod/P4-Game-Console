#!/usr/bin/env python3
"""Reject linked arena state that steals the internal boot/DMA reserve."""
import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('tab5_verify',Path(__file__).resolve().parents[1]/'verify-console-os-tab5.py')
verifier=importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)

class MemoryTests(unittest.TestCase):
    psram_symbols = ('480a0000 b gc\n480b0000 b s_arena_reader\n'
                     '480c0000 b visplanes\n480e0000 b openings\n')

    def test_psram_passes(self):
        verifier.verify_arena_memory(self.psram_symbols)

    def test_internal_missing_and_out_of_range_fail(self):
        for line in self.psram_symbols.splitlines(keepends=True):
            for replacement in ('', '4ff00000' + line[8:], '4a000000' + line[8:]):
                symbols = self.psram_symbols.replace(line, replacement)
                with self.subTest(symbols=symbols), self.assertRaises(ValueError):
                    verifier.verify_arena_memory(symbols)

if __name__=='__main__':
    unittest.main()
