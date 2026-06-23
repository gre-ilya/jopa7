# link_geobabel.pri
#
# Helper to link the prebuilt libgeobabel static library (built from
# geobabel_lib.pro) into your own qmake project, without dragging the GPSBabel
# .cc files into your build -- this avoids object-file name collisions when your
# project already has files like route.cpp / util.cpp.
#
# Usage in your .pro:
#     GEOBABEL_BUILD_DIR = /path/to/where/you/built/geobabel_lib.pro
#     include(/path/to/qt-geo-module/lib/link_geobabel.pri)

isEmpty(GEOBABEL_BUILD_DIR): \
    error("link_geobabel.pri: set GEOBABEL_BUILD_DIR to the directory that \
contains the built libgeobabel.a / geobabel.lib before include(link_geobabel.pri).")

QT       *= core
CONFIG   *= c++17

# Public header (geofile.h) lives in ../src relative to this file.
INCLUDEPATH *= $$clean_path($$PWD/../src)

win32-msvc*|win32-clang-msvc {
    LIBS += $$GEOBABEL_BUILD_DIR/geobabel.lib
} else {
    LIBS += $$GEOBABEL_BUILD_DIR/libgeobabel.a
}
