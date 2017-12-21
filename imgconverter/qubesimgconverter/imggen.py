#!/usr/bin/env python2

'''Qubes Image Generation

Toolkit for generating icons and images for Qubes OS.'''

# The Qubes OS Project, http://www.qubes-os.org
#
# Copyright (C) 2013-2015  Wojtek Porczyk <woju@invisiblethingslab.com>
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

from __future__ import absolute_import

__all__ = ['make_padlock']

import math
import cairo

import qubesimgconverter

def polar(r, a):
    return r * math.cos(a), r * math.sin(a)

def make_padlock(dst, colour, size=qubesimgconverter.ICON_MAXSIZE, disp=False):
    cs = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)

    cr = cairo.Context(cs)
    cr.set_source_rgb(*[c / 256.0
        for c in qubesimgconverter.hex_to_int(colour)])
    cr.set_line_width(.125 * size)

    cr.rectangle(.125 * size, .5 * size, .75 * size, .4375 * size)
    cr.fill()

    cr.move_to(.25 * size, .5 * size)
    cr.line_to(.25 * size, .375 * size)
    cr.arc(.5 * size, .375 * size, .25 * size, math.pi, 2 * math.pi)
    cr.move_to(.75 * size, .375 * size) # this is unneccessary, but helps readability
    cr.line_to(.75 * size, .5 * size)
    cr.stroke()

    if disp:
        # Careful with those. I have run into severe
        # floating point errors when adjusting.
        arrows = 2
        gap = 45 * math.pi / 180
        offset = 0
        radius = .1875 * size
        width = 0.05 * size
        cx = .5 * size
#       cy = .6875 * size
        cy = .625 * size

        arrow = 2 * math.pi / arrows

        for i in range(arrows):
            cr.move_to(cx, cy)
            cr.rel_move_to(*polar(  radius - width,         offset + i * arrow))
            cr.arc(cx, cy,          radius - width,         offset + i * arrow, offset + (i + 1) * arrow - gap)
            cr.rel_line_to(*polar(  width,                  offset + (i + 1) * arrow - gap + math.pi))
            cr.rel_line_to(*polar(  width * math.sqrt(8),   offset + (i + 1) * arrow - gap + math.pi / 4))
            cr.rel_line_to(*polar(  width * math.sqrt(8),   offset + (i + 1) * arrow - gap - math.pi / 4))
            cr.rel_line_to(*polar(  width,                  offset + (i + 1) * arrow - gap + math.pi))
            cr.arc_negative(cx, cy, radius + width,         offset + (i + 1) * arrow - gap, offset + i * arrow)
            cr.close_path()

        cr.set_source_rgb(0xcc / 256.0, 0, 0)  # tango's red
        cr.set_line_width(.0500 * size)
        cr.set_line_join(cairo.LINE_JOIN_ROUND)
        cr.stroke_preserve()

        cr.set_source_rgb(1.0, 1.0, 1.0)
        cr.fill()

    cs.write_to_png(dst)

# vim: ft=python sw=4 ts=4 et
