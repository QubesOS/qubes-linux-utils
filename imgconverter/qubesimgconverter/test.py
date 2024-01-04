from __future__ import absolute_import

import asyncio

try:
    from io import BytesIO
except ImportError:
    from cStringIO import StringIO as BytesIO
import unittest


import qubesimgconverter

class TestCaseImage(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.rgba = \
            b'\x00\x00\x00\xff' b'\xff\x00\x00\xff' \
            b'\x00\xff\x00\xff' b'\x00\x00\x00\xff'
        self.size = (2, 2)

        self.image = qubesimgconverter.Image(rgba=self.rgba, size=self.size)

    def test_00_init(self):
        self.assertEqual(self.image._rgba, self.rgba)
        self.assertEqual(self.image._size, self.size)

    def test_01_tint(self):
        image = self.image.tint('#0000ff')

        self.assertEqual(image._rgba,
            b'\x00\x00\x3f\xff' b'\x00\x00\xff\xff'
            b'\x00\x00\xff\xff' b'\x00\x00\x3f\xff')

    def test_10_get_from_stream(self):
        io = BytesIO('{0[0]} {0[1]}\n'.format(self.size).encode() + self.rgba)

        image = qubesimgconverter.Image.get_from_stream(io)

        self.assertEqual(image._rgba, self.rgba)
        self.assertEqual(image._size, self.size)

    def test_11_get_from_stream_malformed(self):
        io = BytesIO('{0[0]} {0[1]}\n'.format(self.size).encode() +
                     self.rgba[:-1])  # one byte too short

        with self.assertRaisesRegex(ValueError, 'data length violation'):
            image = qubesimgconverter.Image.get_from_stream(io)

    def test_12_get_from_stream_too_big(self):
        io = BytesIO('{0[0]} {0[1]}\n'.format(self.size).encode() + self.rgba)  # 2x2

        with self.assertRaisesRegex(ValueError, 'size constraint violation'):
            image = qubesimgconverter.Image.get_from_stream(io, max_width=1)

        io.seek(0)
        with self.assertRaisesRegex(ValueError, 'size constraint violation'):
            image = qubesimgconverter.Image.get_from_stream(io, max_height=1)

    async def test_20_get_from_stream_async(self):
        reader = asyncio.StreamReader()
        reader.feed_data('{0[0]} {0[1]}\n'.format(self.size).encode() + self.rgba)

        image = await qubesimgconverter.Image.get_from_stream_async(reader)

        self.assertEqual(image._rgba, self.rgba)
        self.assertEqual(image._size, self.size)

    async def test_21_get_from_stream_malformed_async(self):
        reader = asyncio.StreamReader()
        reader.feed_data('{0[0]} {0[1]}\n'.format(self.size).encode() +
                         self.rgba[:-1])  # one byte too short
        reader.feed_eof()

        with self.assertRaises(asyncio.IncompleteReadError):
            image = await qubesimgconverter.Image.get_from_stream_async(reader)

    async def test_22_get_from_stream_too_big(self):
        data = '{0[0]} {0[1]}\n'.format(self.size).encode() + self.rgba  # 2x2

        reader = asyncio.StreamReader()
        reader.feed_data(data)
        with self.assertRaisesRegex(ValueError, 'size constraint violation'):
            image = await qubesimgconverter.Image.get_from_stream_async(reader, max_width=1)

        reader = asyncio.StreamReader()
        reader.feed_data(data)
        with self.assertRaisesRegex(ValueError, 'size constraint violation'):
            image = await qubesimgconverter.Image.get_from_stream_async(reader, max_height=1)

    async def test_23_get_from_stream_header_too_long(self):
        data = '{0[0]} {0[1]}\n'.format(self.size).encode() + self.rgba  # 2x2
        reader = asyncio.StreamReader()
        reader.feed_data(b'x' * 20 + b'\n')
        with self.assertRaisesRegex(ValueError, 'Header too long'):
            image = await qubesimgconverter.Image.get_from_stream_async(reader)


class TestCaseFunctionsAndConstants(unittest.TestCase):
    def test_00_imghdrlen(self):
        self.assertEqual(qubesimgconverter.imghdrlen(8, 15), len('8 15\n'))
        self.assertEqual(qubesimgconverter.imghdrlen(100, 100), len('100 100\n'))

    def test_01_re_imghdr(self):
        self.assertTrue(qubesimgconverter.re_imghdr.match(b'8 15\n'))
        self.assertIsNone(qubesimgconverter.re_imghdr.match(b'8 15'))
        self.assertIsNone(qubesimgconverter.re_imghdr.match(b'815\n'))
        self.assertIsNone(qubesimgconverter.re_imghdr.match(b'x yx\n'))

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
