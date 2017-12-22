# vim: fileencoding=utf-8

#
# The Qubes OS Project, https://www.qubes-os.org/
#
# Copyright (C) 2017
#                   Marek Marczykowski-Górecki <marmarek@invisiblethingslab.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

import itertools
import qubes.tests.extra


# noinspection PyPep8Naming
class TC_00_ImgConverter(qubes.tests.extra.ExtraTestCase):
    def setUp(self):
        super(TC_00_ImgConverter, self).setUp()
        # noinspection PyAttributeOutsideInit
        self.vm = self.create_vms(["vm"])[0]
        self.vm.start()

        self.image_size = 16
        # RGB data for the image
        self.image_data = [
            (0xff // self.image_size * x, 0x80, 0xff // self.image_size * y,
            0xff)
            for x in range(self.image_size)
            for y in range(self.image_size)]

    def create_img(self, filename):
        '''Create image file with given sample content

        :param filename: output filename
        '''
        p = self.vm.run(
            'convert -size {}x{} -depth 8 rgba:- "{}" 2>&1'.format(
                self.image_size, self.image_size, filename),
            passio_popen=True)
        bytes_data = bytes(bytearray(itertools.chain(*self.image_data)))
        (stdout, _) = p.communicate(bytes_data)
        if p.returncode != 0:
            self.skipTest('failed to create test image: {}'.format(stdout))

    def assertCorrectlyTransformed(self, orig_filename, trusted_filename):
        self.assertEquals(
            self.vm.run('test -r "{}"'.format(trusted_filename), wait=True), 0)
        self.assertEquals(
            self.vm.run('test -r "{}"'.format(orig_filename), wait=True), 0)
        # retrieve original image too, to compensate for compression
        p = self.vm.run('convert "{}" rgb:-'.format(orig_filename),
            passio_popen=True)
        orig_image_data, _ = p.communicate()
        p = self.vm.run('convert "{}" rgb:-'.format(trusted_filename),
            passio_popen=True)
        trusted_image_data, _ = p.communicate()
        self.assertEquals(orig_image_data, trusted_image_data)

    def test_000_png(self):
        self.create_img('test.png')
        p = self.vm.run('qvm-convert-img test.png trusted.png 2>&1',
            passio_popen=True)
        (stdout, _) = p.communicate()
        self.assertEquals(p.returncode, 0, 'qvm-convert-img failed: {}'.format(
            stdout))
        self.assertCorrectlyTransformed('test.png', 'trusted.png')

    def test_010_filename_with_spaces(self):
        self.create_img('test with spaces.png')
        p = self.vm.run('qvm-convert-img "test with spaces.png" '
                        '"trusted with spaces.png" 2>&1',
            passio_popen=True)
        (stdout, _) = p.communicate()
        self.assertEquals(p.returncode, 0, 'qvm-convert-img failed: {}'.format(
            stdout))
        self.assertCorrectlyTransformed(
            'test with spaces.png', 'trusted with spaces.png')


def list_tests():
    tests = [TC_00_ImgConverter]
    return tests
