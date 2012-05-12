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

Setup your configuration if you want to::

    cat >config.mak <<\EOF
    prefix=/usr/local
    EOF

then build::

    make && sudo make install


Configuration
-------------

The configuration file ``/etc/chpersroot.conf`` is read to determine how to
enter a chroot.  This file is in INI format, although certain keys can be
multi-valued.

An example file might be::

    [gentoo32]
        rootdir = /gentoo32
        copyfile = /etc/resolv.conf
        personality = linux32

The configuration to use is chosen by the basename of argument zero, in other
words it is the filename by which the program is invoked.  If you only have a
single configuration and don't want to worry about this, just call the
configuration ``chpersroot``, otherwise you should create a symbolic link
somewhere in your path that links from your configuration's name to the
``chpersroot`` executable.


Configuration Keys
~~~~~~~~~~~~~~~~~~

The following configuration keys are available:

``rootdir``
    The path to the new root.
``copyfile``
    A file to be copied into the new root.  This key may be specified multiple
    times if you want to copy multiple files.
``personality``
    The personality for the chroot.  This is one of the ``PER_`` variables
    from ``/usr/include/linux/personality.h`` with the prefix removed and
    underscores converted to hyphens; the comparison is case insensitive.
