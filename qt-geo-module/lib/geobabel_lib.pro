# geobabel_lib.pro
#
# Builds the geo-format parsing module (wrapper + the needed GPSBabel core +
# bundled zlib) as a STATIC LIBRARY: libgeobabel.a (Unix) / geobabel.lib (MSVC).
#
# Why a static library?
#   When you pull the GPSBabel .cc files straight into your own .pro (via
#   geobabel.pri), qmake names every object file after the source's BASE name.
#   If your project already has e.g. route.cpp / util.cpp, it collides with
#   GPSBabel's route.cc / util.cc (both -> route.o / util.o) and one silently
#   overwrites the other, producing "undefined reference" link errors.
#   Building the module separately puts its objects in their own build dir, so
#   there is no collision with your project's files.
#
# Build it (out-of-source recommended):
#     mkdir build-geobabel && cd build-geobabel
#     qmake /path/to/qt-geo-module/lib/geobabel_lib.pro
#     make            # -> libgeobabel.a
#
# Then link it from your own .pro (see lib/link_geobabel.pri for a helper):
#     INCLUDEPATH += /path/to/qt-geo-module/src
#     LIBS        += /path/to/build-geobabel/libgeobabel.a
#     QT          += core

TEMPLATE = lib
CONFIG  += staticlib c++17
CONFIG  -= debug_and_release debug_and_release_target
CONFIG  += release
QT      += core
QT      -= gui
TARGET   = geobabel

# Pull in the wrapper + GPSBabel core + bundled zlib. GPSBABEL_SRC defaults to
# the parent of the module dir; override before include() if your GPSBabel tree
# lives elsewhere:  GPSBABEL_SRC = /path/to/gpsbabel
include($$PWD/../geobabel.pri)
