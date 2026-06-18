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
# GPSBabel core needed to read Garmin .gdb files.
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
    $$GPSBABEL_SRC/src/core/usasciicodec.cc \
    $$GPSBABEL_SRC/src/core/logging.cc \
    $$GPSBABEL_SRC/jeeps/gpsmath.cc

# gbfile.cc uses zlib.
LIBS += -lz

# ---------------------------------------------------------------------------
# To support more GPSBabel formats:
#   1. add that format's .cc file(s) above (and any new dependencies);
#   2. add a branch in makeReader() in geofile.cpp;
#   3. add the extension to GeoFileParser::supportedExtensions().
# For example GPX would add gpx.cc plus xmlgeneric.cc and a few src/core/*.cc.
# ---------------------------------------------------------------------------
