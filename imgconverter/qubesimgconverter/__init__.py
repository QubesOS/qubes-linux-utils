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


import os
import re
try:
    from io import BytesIO
except ImportError:
    from cStringIO import StringIO as BytesIO
import subprocess
import sys
import unittest

import PIL.Image
import numpy

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
def imghdrlen(w, h):
    # width & height are inclusive max vals, and +2 for ' ' and '\n'
    return len(str(w)) + len(str(h)) + 2

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

    def save_pil(self, dst):
        '''Save image to disk using PIL.'''

        img = PIL.Image.frombytes('RGBA', self._size, self._rgba)
        img.save(dst)

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

        tr, tg, tb = hex_to_int(colour)
        tM = max(tr, tg, tb)
        tm = min(tr, tg, tb)

        # (trn/tdn, tgn/tdn, tbn/tdn) is the tint color with maximum saturation
        if tm == tM:
            trn = 1
            tgn = 1
            tbn = 1
            tdn = 2
        else:
            trn = tr - tm
            tgn = tg - tm
            tbn = tb - tm
            tdn = tM - tm

        # use a 1D image representation since we only process a single pixel at a time
        pixels = self._size[0] * self._size[1]
        x = numpy.fromstring(self._rgba, 'B').reshape(pixels, 4)
        r = x[:, 0]
        g = x[:, 1]
        b = x[:, 2]
        a = x[:, 3]
        M = numpy.maximum(numpy.maximum(r, g), b).astype('u4')
        m = numpy.minimum(numpy.minimum(r, g), b).astype('u4')

        # Tn/Td is how much chroma range is reserved for the tint color
        # 0 -> greyscale image becomes greyscale image
        # 1 -> image becomes solid tint color
        Tn = 1
        Td = 4

        # set chroma to the original pixel chroma mapped to the Tn/Td .. 1 range
        # float c2 = (Tn/Td) + (1.0 - Tn/Td) * c

        # set lightness to the original pixel lightness mapped to the range for the new chroma value
        # float m2 = m * (1.0 - c2) / (1.0 - c)

        c = M - m

        c2 = (Tn * 255) + (Td - Tn) * c
        c2d = Td

        m2 = ((255 * c2d) - c2) * m
        # the maximum avoids division by 0 when c = 255 (m2 is 0 anyway, so m2d doesn't matter)
        m2d = numpy.maximum((255 - c) * c2d, 1)

        # precomputed values
        c2d_tdn = tdn * c2d
        m2_c2d_tdn = m2 * c2d_tdn
        m2d_c2d_tdn = m2d * c2d_tdn
        c2_m2d = c2 * m2d

        # float vt = m2 + tvn * c2
        rt = ((m2_c2d_tdn + trn * c2_m2d) // m2d_c2d_tdn).astype('B')
        gt = ((m2_c2d_tdn + tgn * c2_m2d) // m2d_c2d_tdn).astype('B')
        bt = ((m2_c2d_tdn + tbn * c2_m2d) // m2d_c2d_tdn).astype('B')

        xt = numpy.column_stack((rt, gt, bt, a))
        return self.__class__(rgba=xt.tobytes(), size=self._size)

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

    def load_from_file_pil(cls, filename):
        '''Loads image from local file using PIL.'''
        img = PIL.Image.open(filename)
        img = img.convert('RGBA')
        return cls(rgba=img.tobytes(), size=img.size)

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

def hex_to_int(colour, channels=3, depth=1):
    '''Convert hex colour definition to tuple of ints.'''

    length = channels * depth * 2
    step = depth * 2

    # get rid of '#' or '0x' in front of hex values
    colour = colour[-length:]

    return tuple(int(colour[i:i+step], 0x10) for i in range(0, length, step))

def tint(src, dst, colour):
    '''Tint image to reflect vm label.

    src and dst may NOT specify ImageMagick format'''

    Image.load_from_file_pil(src).tint(colour).save_pil(dst)


# vim: ft=python sw=4 ts=4 et
