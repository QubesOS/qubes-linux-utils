#!/usr/bin/python2 -O

'''Qubes Image Converter

Toolkit for secure transfer and conversion of images between Qubes VMs.'''

# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2013  Wojciech Porczyk <wojciech@porczyk.eu>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.


import colorsys
import math
import os
import re
try:
    from io import BytesIO
except ImportError:
    from cStringIO import StringIO as BytesIO
import subprocess
import sys
import unittest

import cairo

# those are for "zOMG UlTRa HD! WalLLpapPer 8K!!1!" to work seamlessly;
# 8192 * 5120 * 4 B = 160 MiB, so DoS by memory exhaustion is unlikely
MAX_WIDTH = 8192
MAX_HEIGHT = 5120

# current max raster icon size in hicolor theme is 256 as of 2013/fedora-18
# beyond that one probably shall use scalable icons
# (SVG is currently unsupported)
ICON_MAXSIZE = 512

# header consists of two decimal numbers, SPC and LF
re_imghdr = re.compile(br'^\d+ \d+\n$')
imghdrlen = lambda w, h: int(math.ceil(math.log10(w)) \
    + math.ceil(math.log10(h)) \
    + 2)

class Image(object):
    def __init__(self, rgba, size):
        '''This class is not meant to be instantiated directly. Use one of:
get_from_stream(), get_from_vm(), get_xdg_icon_from_vm(), get_through_dvm()'''
        self._rgba = rgba
        self._size = size

    def save(self, dst):
        'Save image to disk. dst may specify format, like png:aqq.gif'

        p = subprocess.Popen(['convert',
            '-depth', '8',
            '-size', '{0[0]}x{0[1]}'.format(self._size),
            'rgba:-', dst], stdin=subprocess.PIPE)
        p.stdin.write(self._rgba)
        p.stdin.close()

        if p.wait():
            raise Exception('Conversion failed')

    @property
    def data(self):
        return self._rgba

    @property
    def width(self):
        return self._size[0]

    @property
    def height(self):
        return self._size[1]

    def tint(self, colour):
        '''Return new tinted image'''

        r, g, b = hex_to_float(colour)
        h, _, s = colorsys.rgb_to_hls(r, g, b)
        result = BytesIO()

        # duplicate the whole loop for performance reasons
        if sys.version_info[0] < 3:
            for i in range(0, self._size[0] * self._size[1] * 4, 4):
                r, g, b, a = tuple(ord(c) / 255. for c in self._rgba[i:i+4])
                _, l, _ = colorsys.rgb_to_hls(r, g, b)
                r, g, b = colorsys.hls_to_rgb(h, l, s)

                result.write(b''.join(chr(int(i * 255)) for i in [r, g, b, a]))
        else:
            for i in range(0, self._size[0] * self._size[1] * 4, 4):
                r, g, b, a = tuple(c / 255. for c in self._rgba[i:i + 4])
                _, l, _ = colorsys.rgb_to_hls(r, g, b)
                r, g, b = colorsys.hls_to_rgb(h, l, s)

                result.write(bytes(int(i * 255) for i in [r, g, b, a]))

        return self.__class__(rgba=result.getvalue(), size=self._size)

    @classmethod
    def load_from_file(cls, filename):
        '''Loads image from local file.

        WARNING: always load trusted images.'''

        p = subprocess.Popen(['identify', '-format', '%w %h', filename],
            stdout=subprocess.PIPE)
        size = tuple(int(i) for i in p.stdout.read().strip().split())
        p.stdout.close()
        p.wait()

        p = subprocess.Popen(['convert', filename, '-depth', '8', 'rgba:-'],
            stdout=subprocess.PIPE)
        rgba = p.stdout.read()
        p.stdout.close()
        p.wait()

        return cls(rgba=rgba, size=size)

    @classmethod
    def get_from_stream(cls, stream, max_width=MAX_WIDTH, max_height=MAX_HEIGHT):
        '''Carefully parse image data from stream.

        THIS METHOD IS SECURITY-SENSITIVE'''

        maxhdrlen = imghdrlen(max_width, max_height)

        untrusted_header = stream.readline(maxhdrlen)
        if len(untrusted_header) == 0:
            raise ValueError('No icon received')
        if not re_imghdr.match(untrusted_header):
            raise ValueError('Image format violation')
        header = untrusted_header
        del untrusted_header

        untrusted_width, untrusted_height = (int(i) for i in header.rstrip().split())
        if not (0 < untrusted_width <= max_width \
                and 0 < untrusted_height <= max_height):
            raise ValueError('Image size constraint violation:'
                    ' width={width} height={height}'
                    ' max_width={max_width} max_height={max_height}'.format(
                        width=untrusted_width, height=untrusted_height,
                        max_width=max_width, max_height=max_height))
        width, height = untrusted_width, untrusted_height
        del untrusted_width, untrusted_height

        expected_data_len = width * height * 4    # RGBA
        untrusted_data = stream.read(expected_data_len)
        if len(untrusted_data) != expected_data_len:
            raise ValueError( \
                'Image data length violation (is {0}, should be {1})'.format( \
                len(untrusted_data), expected_data_len))
        data = untrusted_data
        del untrusted_data

        return cls(rgba=data, size=(width, height))

    @classmethod
    def get_from_vm(cls, vm, src, **kwargs):
        'Get image from VM by QUBESRPC (qubes.GetImageRGBA).'

        p = vm.run_service('qubes.GetImageRGBA')
        p.stdin.write('{0}\n'.format(src).encode())
        p.stdin.close()

        try:
            img = cls.get_from_stream(p.stdout, **kwargs)
        finally:
            p.stdout.close()
        if p.wait():
            raise Exception('Something went wrong with receiver')

        return img

    @classmethod
    def get_xdg_icon_from_vm(cls, vm, icon, **kwargs):
        'Get image from VM. If path is not absolute, get it from hicolor theme.'

        if not os.path.isabs(icon):
            icon = 'xdgicon:' + icon
        return cls.get_from_vm(vm, icon,
            max_width=ICON_MAXSIZE, max_height=ICON_MAXSIZE, **kwargs)

    @classmethod
    def get_through_dvm(cls, filename, **kwargs):
        '''Master end of image filter: writes untrusted image to stdout and
expects header+RGBA on stdin. This method is invoked from qvm-imgconverter-client.'''

        filetype = None
        if ':' in filename:
            filetype, filename = filename.split(':', 1)[0]
            sys.stdout.write('{0}:-\n'.format(filetype))
        else:
            sys.stdout.write('-\n')

        try:
            sys.stdout.write(open(filename).read())
        except Exception as e:
            raise Exception('Something went wrong: {0!s}'.format(e))
        finally:
            sys.stdout.close()
            # sys.stdout.close() is not enough and documentation is silent about this
            os.close(1)

        return cls.get_from_stream(sys.stdin, **kwargs)

    def __eq__(self, other):
        return self._size == other._size and self._rgba == other._rgba

    def __ne__(self, other):
        return not self.__eq__(other)

def hex_to_float(colour, channels=3, depth=8):
    '''Convert hex colour definition to tuple of floats.'''

    if depth % 4 != 0:
        raise NotImplementedError('depths not divisible by 4 are unsupported')

    length = channels * depth // 4
    step = depth // 4

    # get rid of '#' or '0x' in front of hex values
    colour = colour[-length:]

    return tuple(int(colour[i:i+step], 0x10) / float(2**depth - 1) for i in range(0, length, step))

def tint(src, dst, colour):
    '''Tint image to reflect vm label.

    src and dst may specify format, like png:aqq.gif'''

    Image.load_from_file(src).tint(colour).save(dst)


# vim: ft=python sw=4 ts=4 et
