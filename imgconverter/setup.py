import setuptools

setuptools.setup(
    name='qubesimgconverter',
    version=open('../version').read().strip(),
    author='Invisible Things Lab',
    author_email='woju@invisiblethingslab.com',
    description='Toolkit for secure transfer and conversion of images between Qubes VMs.',
    license='GPL2+',
    url='https://www.qubes-os.org/',
    packages=['qubesimgconverter'],
)

# vim: ts=4 sts=4 sw=4 et
