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


Configuration
-------------

Currently all configuration is performed at build time via the ``config.mak``
file.  The available variables are:

``PROG_NAME``
    The name of the executable program to build.
``ROOT_DIR``
    The root directory of your chroot.
``COPY_IN``
    The absolute path of a file to be copied into the chroot when the program
    is run.  For example, this can be set to ``/etc/resolv.conf`` to update
    the chroot environments nameserver settings whenever you enter it.
``prefix``
    The installation prefix.
