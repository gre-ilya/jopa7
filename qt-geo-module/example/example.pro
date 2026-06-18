# Drag-and-drop GUI demo for the geo::GeoFileParser module.
#
# Build:
#     qmake && make
# Run (then drag a .gdb file onto the window):
#     ./geofileviewer
# Or pass a file directly:
#     ./geofileviewer /path/to/file.gdb

QT       += core gui widgets
CONFIG   += c++17
CONFIG   -= debug_and_release debug_and_release_target
CONFIG   += release
TEMPLATE  = app
TARGET    = geofileviewer

# Pull in the parsing module (which itself pulls in the needed GPSBabel core).
# GPSBABEL_SRC defaults to the parent of the module directory, which is correct
# because this module lives inside the GPSBabel source tree.
include(../geobabel.pri)

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h
