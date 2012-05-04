chpersroot: Personal+Personality Chroot
=======================================

A program to simplify the use of 32-bit chroots on 64-bit non-multilib Linux
systems.

Basically, this program is equivalent to running::

    sudo linux32 chroot /path/to/my/chroot su -l "$USER" [args]

but because it's installed setuid you don't need to type a password and the
command line is somewhat shorter because the location of the chroot is
compiled in at build time.


Build Instructions
------------------

Setup your configuration::

    cat >config.mak <<\EOF
    PROG_NAME=mychpersroot
    ROOT_DIR=/path/to/my/chroot
    prefix=/usr/local
    EOF

then build::

    make && sudo make install
