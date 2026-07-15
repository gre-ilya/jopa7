# geobabel.pri
#
# qmake include file that adds the GPSBabel-based geo-format parsing module
# (geo::GeoFileParser) to your project.
#
# Usage from your .pro file:
#
#     # Optional: point at the GPSBabel source tree.  Defaults to the parent
#     # of this module directory, which is correct when the module lives inside
#     # the GPSBabel source checkout (as it does in this repository).
#     # GPSBABEL_SRC = /path/to/gpsbabel-source
#
#     include(/path/to/qt-geo-module/geobabel.pri)
#
# Then in your code:
#
#     #include "geofile.h"
#     geo::GeoFileParser parser;
#     geo::GeoData data;
#     QString err;
#     if (parser.parse("/some/file.gdb", data, &err)) { ... }
#
# The module needs only QtCore; the GUI example additionally uses widgets.

QT *= core

CONFIG *= c++17

# ---------------------------------------------------------------------------
# Locate the GPSBabel source tree.
# ---------------------------------------------------------------------------
isEmpty(GPSBABEL_SRC) {
    # This .pri sits in <gpsbabel>/qt-geo-module/, so the tree is one level up.
    GPSBABEL_SRC = $$clean_path($$PWD/..)
}

# Fail fast with an actionable message instead of a wall of "undefined
# reference" linker errors later: verify the GPSBabel sources really are where
# we are about to look for them.
!exists($$GPSBABEL_SRC/gdb.cc)|!exists($$GPSBABEL_SRC/route.cc)|!exists($$GPSBABEL_SRC/defs.h) {
    error("geobabel.pri: GPSBabel sources not found under GPSBABEL_SRC='$$GPSBABEL_SRC'. \
Set GPSBABEL_SRC to the root of the GPSBabel source tree BEFORE include(.../geobabel.pri), e.g.:$$escape_expand(\\n)\
    GPSBABEL_SRC = /path/to/gpsbabel$$escape_expand(\\n)\
    include(/path/to/qt-geo-module/geobabel.pri)$$escape_expand(\\n)\
The directory must contain gdb.cc, route.cc, waypt.cc, defs.h, jeeps/ and src/core/. \
Do NOT add the GPSBabel .cc files to your own SOURCES by hand -- this .pri adds the complete set.")
}

MODULE_SRC = $$PWD/src

INCLUDEPATH *= $$MODULE_SRC      # geofile.h
INCLUDEPATH *= $$PWD             # our static gbversion.h (replaces the
                                 # CMake-generated one)
INCLUDEPATH *= $$GPSBABEL_SRC    # defs.h, gdb.h, jeeps/..., src/core/... etc.

# ---------------------------------------------------------------------------
# Module wrapper sources.
# ---------------------------------------------------------------------------
HEADERS += $$MODULE_SRC/geofile.h
SOURCES += $$MODULE_SRC/geofile.cpp
# gb_fatal.cpp REPLACES the upstream fatal.cc: it throws on a fatal parse error
# instead of calling exit(1), so a bad file cannot kill the host application.
SOURCES += $$MODULE_SRC/gb_fatal.cpp

# ---------------------------------------------------------------------------
# GPSBabel core needed to read Garmin .gdb and GPX files.
# (Determined as the minimal closure for the GDB reader; note fatal.cc is
#  deliberately NOT listed -- see gb_fatal.cpp above.)
# ---------------------------------------------------------------------------
SOURCES += \
    $$GPSBABEL_SRC/globals.cc \
    $$GPSBABEL_SRC/util.cc \
    $$GPSBABEL_SRC/waypt.cc \
    $$GPSBABEL_SRC/route.cc \
    $$GPSBABEL_SRC/session.cc \
    $$GPSBABEL_SRC/formspec.cc \
    $$GPSBABEL_SRC/gbfile.cc \
    $$GPSBABEL_SRC/mkshort.cc \
    $$GPSBABEL_SRC/geocache.cc \
    $$GPSBABEL_SRC/garmin_fs.cc \
    $$GPSBABEL_SRC/garmin_tables.cc \
    $$GPSBABEL_SRC/grtcirc.cc \
    $$GPSBABEL_SRC/inifile.cc \
    $$GPSBABEL_SRC/gdb.cc \
    $$GPSBABEL_SRC/gpx.cc \
    $$GPSBABEL_SRC/src/core/xmlstreamwriter.cc \
    $$GPSBABEL_SRC/src/core/xmltag.cc \
    $$GPSBABEL_SRC/src/core/usasciicodec.cc \
    $$GPSBABEL_SRC/src/core/logging.cc \
    $$GPSBABEL_SRC/jeeps/gpsmath.cc

# ---------------------------------------------------------------------------
# Platform defines.
#   defs.h already defines M_PI itself, so _USE_MATH_DEFINES is not needed.
# ---------------------------------------------------------------------------
win32: DEFINES *= __WIN32__
# Silence MSVC-CRT deprecation noise.  Applied to every Windows toolchain
# (MSVC, clang-cl / win32-clang-msvc, and MinGW / clang-MinGW); harmless where
# the MS CRT headers are not used.
win32: DEFINES *= _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS

# ---------------------------------------------------------------------------
# zlib.  gbfile.cc uses zlib to read gzip-compressed inputs.  By default we
# compile GPSBabel's BUNDLED zlib (its own default, "included" mode) so the
# module is self-contained and builds identically on Windows and Linux with no
# external dependency.
#
#   * Default            -> bundled zlib from $$GPSBABEL_SRC/zlib
#   * GEOBABEL_ZLIB=system  -> use a system zlib (-lz, needs HAVE_LIBZ headers)
#   * GEOBABEL_ZLIB=none    -> no zlib; gzip-compressed inputs are unsupported
#                             (plain .gdb is never gzip-compressed, so this is
#                              fine if you only read .gdb)
# ---------------------------------------------------------------------------
isEmpty(GEOBABEL_ZLIB): GEOBABEL_ZLIB = bundled

equals(GEOBABEL_ZLIB, system) {
    DEFINES *= HAVE_LIBZ
    # MSVC-style drivers (MSVC and clang-cl) link the import library by name;
    # everything else (gcc, MinGW, clang-MinGW, Unix) uses -lz.
    win32-msvc*|win32-clang-msvc: LIBS += zlib.lib
    else:                         LIBS += -lz
} else:equals(GEOBABEL_ZLIB, none) {
    DEFINES *= ZLIB_INHIBITED
} else {
    # bundled (default)
    INCLUDEPATH *= $$GPSBABEL_SRC/zlib
    unix: DEFINES *= HAVE_UNISTD_H HAVE_STDARG_H
    SOURCES += \
        $$GPSBABEL_SRC/zlib/adler32.c \
        $$GPSBABEL_SRC/zlib/compress.c \
        $$GPSBABEL_SRC/zlib/crc32.c \
        $$GPSBABEL_SRC/zlib/deflate.c \
        $$GPSBABEL_SRC/zlib/inffast.c \
        $$GPSBABEL_SRC/zlib/inflate.c \
        $$GPSBABEL_SRC/zlib/infback.c \
        $$GPSBABEL_SRC/zlib/inftrees.c \
        $$GPSBABEL_SRC/zlib/trees.c \
        $$GPSBABEL_SRC/zlib/uncompr.c \
        $$GPSBABEL_SRC/zlib/gzlib.c \
        $$GPSBABEL_SRC/zlib/gzclose.c \
        $$GPSBABEL_SRC/zlib/gzread.c \
        $$GPSBABEL_SRC/zlib/gzwrite.c \
        $$GPSBABEL_SRC/zlib/zutil.c
}

# ---------------------------------------------------------------------------
# To support more GPSBabel formats:
#   1. add that format's .cc file(s) above (and any new dependencies);
#   2. add a branch in makeReader() in geofile.cpp;
#   3. add the extension to GeoFileParser::supportedExtensions().
# For example GPX would add gpx.cc plus xmlgeneric.cc and a few src/core/*.cc.
# ---------------------------------------------------------------------------
