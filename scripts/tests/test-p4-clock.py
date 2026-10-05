#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import struct
import unittest
from unittest import mock
from types import SimpleNamespace
import io
import json
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("p4_clock",ROOT/"scripts/p4-transfer.py")
tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
class ClockTests(unittest.TestCase):
    def reply(self):
        data=bytearray(struct.pack("<4sBBBBIIiI",b"P4L1",1,2,0,3,123,1709251139,0,0))
        return data+struct.pack("<I",tool.crc32(data))
    def test_request_range_and_crc(self):
        data=tool.make_clock_request(2,123,4102444799)
        self.assertEqual(len(data),20)
        self.assertEqual(struct.unpack_from("<I",data,16)[0],tool.crc32(data[:16]))
        for args in ((2,0,1709251139),(2,1,946684799),(2,1,4102444800),(1,1,1),(9,1,0)):
            with self.assertRaises(tool.TransferError):tool.make_clock_request(*args)
    def test_response_and_utc(self):
        response=tool.parse_clock_response(self.reply(),2,123)
        self.assertEqual(response["utc"],"2024-02-29T23:58:59+00:00")
        self.assertTrue(response["valid"])
    def test_bad_crc_nonce_reserved_flags(self):
        for offset in (4,5,7,8,20,24):
            data=self.reply();data[offset]^=0x80
            if offset!=24:struct.pack_into("<I",data,24,tool.crc32(data[:24]))
            with self.assertRaises(tool.TransferError):tool.parse_clock_response(data,2,123)
        with self.assertRaises(tool.TransferError):tool.parse_clock_response(self.reply()[:-1],2,123)
    def status_reply(self, present, error=0):
        flags = 3 if present else 0
        seconds = 1709251139 if present else 0
        data = struct.pack("<4sBBBBIIiI", b"P4L1", 1, 1, 0, flags, 123, seconds, error, 0)
        return data + struct.pack("<I", tool.crc32(data))

    def run_status(self, replies):
        output = io.StringIO()
        reader = mock.Mock()
        reader.frame.side_effect = replies
        with mock.patch.object(tool, "open_port"), \
             mock.patch.object(tool, "WireReader", return_value=reader), \
             mock.patch.object(tool, "write_all") as write, \
             mock.patch.object(tool.secrets, "randbits", return_value=123), \
             mock.patch.object(tool.time, "sleep"), \
             mock.patch("sys.stdout", output):
            tool.clock_command(SimpleNamespace(port="test-port", sync=False))
        self.assertTrue(all(call.args[1][5] == 1 for call in write.call_args_list))
        return json.loads(output.getvalue()), reader.frame.call_count

    def test_status_waits_for_async_sensor_probe(self):
        result, calls = self.run_status([self.status_reply(False), self.status_reply(True)])
        self.assertTrue(result["present"] and result["valid"])
        self.assertEqual(calls, 2)

    def test_status_reports_real_probe_failure(self):
        result, calls = self.run_status([self.status_reply(False, error=263)])
        self.assertFalse(result["present"])
        self.assertEqual(result["error"], 263)
        self.assertEqual(calls, 1)

if __name__=="__main__":unittest.main()
