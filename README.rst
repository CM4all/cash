cachefilesd
===========

The Linux ``cachefiles`` userspace daemon.


Building cachefiles
-------------------

You need:

- Linux kernel with ``CONFIG_CACHEFILES=y``
- a C++20 compliant compiler
- `libfmt <https://fmt.dev/>`__
- `Meson 1.0 <http://mesonbuild.com/>`__ and `Ninja <https://ninja-build.org/>`__

Optional dependencies:

- `systemd <https://www.freedesktop.org/wiki/Software/systemd/>`__
- `libcap2 <https://sites.google.com/site/fullycapable/>`__ for
  dropping unnecessary Linux capabilities

Get the source code::

 git clone --recursive https://github.com/CM4all/cash.git

Run ``meson``::

 meson setup output

Compile and install::

 ninja -C output
 ninja -C output install


Building the Debian package
---------------------------

After installing the build dependencies (``dpkg-checkbuilddeps``),
run::

 dpkg-buildpackage -rfakeroot -b -uc -us
