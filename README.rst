cash
====

``cash`` is an alternative implementation of the Linux ``cachefiles``
userspace daemon.  Unlike ``cachefilesd`` (the primary
implementation), it uses efficient data structures and ``io_uring``,
which makes it scale well to multi-terabyte cache partitions.

We developed this project because ``cachefilesd`` did not scale well
with large cache partitions; for months, it ran at 100% CPU without
making any progress, effectively disabling the cache because no old
files were discarded and therefore no new files could be added.  The
reason was that ``cachefilesd`` keeps its file list in a sorted array,
which is extremely inefficient.  Improving this old C code base did
not seem worth it, because a new implementation in C++ took less time
than that.

The name "cash" is a play on the pronounciation of "cache" vs "cash".
Maybe this project deserves a better name...


Building cash
-------------

You need:

- Linux kernel with ``CONFIG_CACHEFILES=y``
- a C++20 compliant compiler
- `libfmt <https://fmt.dev/>`__
- `Meson 1.2 <http://mesonbuild.com/>`__ and `Ninja <https://ninja-build.org/>`__

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
