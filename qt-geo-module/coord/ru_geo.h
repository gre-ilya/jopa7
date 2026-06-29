// ru_geo.h
//
// 7-parameter (Helmert) datum transformations between the three reference
// systems used in Russian geodesy:
//
//     WGS-84  <->  PZ-90.11 (ПЗ-90.11)  <->  SK-42 (СК-42 / Pulkovo-1942)
//
// The transforms follow the model and convention of ГОСТ 32453-2017
// "Глобальная навигационная спутниковая система. Системы координат. Методы
// преобразований координат определяемых точек". PZ-90.11 is used as the hub:
// any pair is converted by chaining  A -> PZ-90.11 -> B  in geocentric
// (ECEF) space.
//
// This module is self-contained (only <cmath>/<string>) and does NOT depend on
// GPSBabel / jeeps. Angles in the public API are decimal degrees; heights and
// ECEF coordinates are metres (ellipsoidal height for geodetic).
//
// !!! ВАЖНО: числовые параметры в ru_geo.cpp ДОЛЖНЫ быть сверены с вашим
// !!! экземпляром ГОСТ 32453-2017 перед использованием в продакшене.
// !!! См. блок DATUM TRANSFORM PARAMETERS в ru_geo.cpp.

#ifndef RU_GEO_H
#define RU_GEO_H

#include <string>

namespace rugeo {

// Supported reference systems.
enum class Datum {
  WGS84,   // WGS-84 (G1150)
  PZ9011,  // ПЗ-90.11
  SK42,    // СК-42 (Pulkovo 1942, Krassovsky 1940 ellipsoid)
};

// Geodetic position. lat/lon in decimal degrees, h = ellipsoidal height (m).
struct Geodetic {
  double lat = 0.0;
  double lon = 0.0;
  double h   = 0.0;
};

// Geocentric (Earth-Centred, Earth-Fixed) Cartesian position, metres.
struct ECEF {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

// --- High-level API ---------------------------------------------------------

// Convert a geodetic position from one datum to another (degrees in/out).
// If from == to the input is returned unchanged.
Geodetic convert(const Geodetic& in, Datum from, Datum to);

// Human-readable datum name (e.g. "СК-42").
const char* datumName(Datum d);

// --- Building blocks (exposed for testing / advanced use) -------------------

// Geodetic <-> ECEF on the ellipsoid associated with `d`.
ECEF     geodeticToEcef(const Geodetic& g, Datum d);
Geodetic ecefToGeodetic(const ECEF& e, Datum d);

// 7-parameter Helmert transform of an ECEF position between two datum frames.
ECEF helmert(const ECEF& in, Datum from, Datum to);

}  // namespace rugeo

#endif  // RU_GEO_H
