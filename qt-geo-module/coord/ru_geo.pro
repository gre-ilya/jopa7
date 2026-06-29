# ru_geo.pro -- builds the WGS-84 / ПЗ-90.11 / СК-42 transform self-test.
#
#   qmake ru_geo.pro && make && ./ru_geo_test
#
# The module itself (ru_geo.cpp/.h) is plain C++17 with no Qt or GPSBabel
# dependency -- you can also just add those two files to your own project.

TEMPLATE = app
CONFIG  += c++17 console
CONFIG  -= app_bundle qt
TARGET   = ru_geo_test

SOURCES += ru_geo.cpp ru_geo_test.cpp
HEADERS += ru_geo.h
