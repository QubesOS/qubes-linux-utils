#!/usr/bin/env python2

from __future__ import absolute_import

import cStringIO as StringIO
import unittest

import qubesimgconverter

class TestCaseImage(unittest.TestCase):
    def setUp(self):
        self.rgba = \
            '\x00\x00\x00\xff' '\xff\x00\x00\xff' \
            '\x00\xff\x00\xff' '\x00\x00\x00\xff'
        self.size = (2, 2)

        self.image = qubesimgconverter.Image(rgba=self.rgba, size=self.size)

    def test_00_init(self):
        self.assertEqual(self.image._rgba, self.rgba)
        self.assertEqual(self.image._size, self.size)

    def test_01_tint(self):
        image = self.image.tint('#0000ff')

        self.assertEqual(image._rgba,
            '\x00\x00\x3f\xff' '\x00\x00\xff\xff'
            '\x00\x00\xff\xff' '\x00\x00\x3f\xff')

    def test_10_get_from_stream(self):
        io = StringIO.StringIO('{0[0]} {0[1]}\n{1}'.format(self.size, self.rgba))

        image = qubesimgconverter.Image.get_from_stream(io)

        self.assertEqual(image._rgba, self.rgba)
        self.assertEqual(image._size, self.size)

    def test_11_get_from_stream_malformed(self):
        io = StringIO.StringIO('{0[0]} {0[1]}\n{1}'.format(self.size, self.rgba[-1])) # one byte too short

        with self.assertRaises(Exception):
            image = qubesimgconverter.Image.get_from_stream(io)

    def test_12_get_from_stream_too_big(self):
        io = StringIO.StringIO('{0[0]} {0[1]}\n{1}'.format(self.size, self.rgba)) # 2x2

        with self.assertRaises(Exception):
            image = qubesimgconverter.Image.get_from_stream(io, max_width=1)

        io.seek(0)
        with self.assertRaises(Exception):
            image = qubesimgconverter.Image.get_from_stream(io, max_height=1)

class TestCaseFunctionsAndConstants(unittest.TestCase):
    def test_00_imghdrlen(self):
        self.assertEqual(qubesimgconverter.imghdrlen(8, 15), len('8 15\n'))
        self.assertEqual(qubesimgconverter.imghdrlen(100, 100), len('100 100\n'))

    def test_01_re_imghdr(self):
        self.assertTrue(qubesimgconverter.re_imghdr.match('8 15\n'))
        self.assertIsNone(qubesimgconverter.re_imghdr.match('8 15'))
        self.assertIsNone(qubesimgconverter.re_imghdr.match('815\n'))
        self.assertIsNone(qubesimgconverter.re_imghdr.match('x yx\n'))

    def test_10_hex_to_float_result_00(self):
        self.assertEqual(qubesimgconverter.hex_to_int('#000000'), (0, 0, 0))

    def test_11_hex_to_float_result_ff(self):
        self.assertEqual(qubesimgconverter.hex_to_int('0xffffff'),
            (0xff, 0xff, 0xff))

    def test_12_hex_to_float_depth_3_not_implemented(self):
        with self.assertRaises(ValueError):
            qubesimgconverter.hex_to_int('123456', depth=3)

if __name__ == '__main__':
    unittest.main()
