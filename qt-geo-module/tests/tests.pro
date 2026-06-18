# Console self-test for the geo::GeoFileParser module.
#
#     qmake && make && ./geotests
#
# Builds without widgets (console only) so it runs headless on any CI runner.

QT       += core
QT       -= gui
CONFIG   += c++17 console
CONFIG   -= app_bundle
# Force a single-config build so the binary lands in a predictable place on
# every platform (Windows qmake otherwise creates debug/ and release/ subdirs).
CONFIG   -= debug_and_release debug_and_release_target
CONFIG   += release
TEMPLATE  = app
TARGET    = geotests

include(../geobabel.pri)

# Absolute path to the bundled sample so the test can find it from any CWD.
DEFINES += GEO_TEST_DATA_DIR=\\\"$$PWD/data\\\"

SOURCES += main.cpp
