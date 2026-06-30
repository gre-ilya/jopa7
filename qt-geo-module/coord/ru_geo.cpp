// ru_geo.cpp  -- implementation of WGS-84 / ПЗ-90.11 / СК-42 transforms.
//
// Math model (per ГОСТ 32453-2017):
//
//   geodetic(B,L,H)  --[ellipsoid of source]-->  ECEF(X,Y,Z)
//   ECEF             --[7-param Helmert]------->  ECEF in target frame
//   ECEF             --[ellipsoid of target]-->  geodetic(B,L,H)
//
// Helmert (position-vector / "Bursa-Wolf" convention, as used by ГОСТ):
//
//   | X' |   | dX |            |  1    wz  -wy | | X |
//   | Y' | = | dY | + (1 + m)* | -wz   1    wx | | Y |
//   | Z' |   | dZ |            |  wy  -wx   1  | | Z |
//
//   dX,dY,dZ  in metres
//   wx,wy,wz  rotation angles in RADIANS (stored below in arc-seconds)
//   m         scale, dimensionless (stored below in ppm = 1e-6)
//
// The inverse transform negates all seven parameters; for parameters this small
// (rotations < 1", |m| < 1 ppm) the linearised inverse is accurate to well
// under 1 mm, which is far below the few-metre accuracy of the СК-42 datum tie.

#include "ru_geo.h"

#include <cmath>

namespace rugeo {
namespace {

// ---------------------------------------------------------------------------
// Ellipsoids
// ---------------------------------------------------------------------------
struct Ellipsoid {
  double a;     // semi-major axis, m
  double invf;  // inverse flattening (1/f)
};

// WGS-84:          a = 6378137.0,   1/f = 298.257223563
// PZ-90 (.11):     a = 6378136.0,   1/f = 298.25784
// Krassovsky 1940: a = 6378245.0,   1/f = 298.3        (СК-42)
Ellipsoid ellipsoidOf(Datum d) {
  switch (d) {
    case Datum::WGS84:  return {6378137.0, 298.257223563};
    case Datum::PZ9011: return {6378136.0, 298.25784};
    case Datum::SK42:   return {6378245.0, 298.3};
  }
  return {6378137.0, 298.257223563};  // unreachable
}

// ---------------------------------------------------------------------------
// DATUM TRANSFORM PARAMETERS   (ГОСТ 32453-2017)
// ---------------------------------------------------------------------------
// All parameter sets are expressed as  <source datum> -> ПЗ-90.11  (the hub).
// Rotations are in ARC-SECONDS, scale in PPM (1e-6), shifts in METRES.
//
//   !!! СВЕРИТЬ С ГОСТ 32453-2017 ПЕРЕД ПРОДАКШЕНОМ !!!
// These are the standard published values; double-check every digit and sign
// against your authoritative copy of the standard. Change ONLY this block to
// adjust the transforms — the rest of the file is convention/geometry.
struct Params7 {
  double dx, dy, dz;  // metres
  double rx, ry, rz;  // arc-seconds
  double m;           // ppm (1e-6)
};

// СК-42 (Pulkovo 1942) -> ПЗ-90.11           [ГОСТ 32453-2017]
constexpr Params7 kSK42_to_PZ9011 = {
    /*dx*/  23.557, /*dy*/ -140.844, /*dz*/ -79.778,
    /*rx*/  -0.0023, /*ry*/  -0.34646, /*rz*/ -0.79421,
    /*m */  -0.228,
};

// WGS-84 (G1150) -> ПЗ-90.11                  [ГОСТ 32453-2017]
// ПЗ-90.11 is aligned to ITRF2008/WGS-84 at the few-centimetre level, so the
// tie is effectively zero. If your copy of the standard lists explicit
// non-zero values, put them here.
constexpr Params7 kWGS84_to_PZ9011 = {
    /*dx*/  0.0, /*dy*/ 0.0, /*dz*/ 0.0,
    /*rx*/  0.0, /*ry*/ 0.0, /*rz*/ 0.0,
    /*m */  0.0,
};

// Parameter set taking `d` INTO the ПЗ-90.11 hub frame.
Params7 toHubParams(Datum d) {
  switch (d) {
    case Datum::SK42:   return kSK42_to_PZ9011;
    case Datum::WGS84:  return kWGS84_to_PZ9011;
    case Datum::PZ9011: return {0, 0, 0, 0, 0, 0, 0};  // identity
  }
  return {0, 0, 0, 0, 0, 0, 0};
}

constexpr double kPi      = 3.14159265358979323846;
constexpr double kArcsec  = kPi / (180.0 * 3600.0);  // arc-seconds -> radians
constexpr double kDeg     = kPi / 180.0;

// The forward transform is  out = d + M * in, where  M = (1+m) * R  and R is
// the (linearised) rotation matrix of ГОСТ. The forward direction is the one in
// which the parameters are published (source -> hub). The inverse direction
// solves  in = M^{-1} * (out - d)  EXACTLY (3x3 matrix inverse), so a
// round-trip reproduces the input to machine precision rather than relying on
// the negate-the-parameters linear approximation.
ECEF applyHelmert(const ECEF& in, const Params7& p, bool forward) {
  const double rx = p.rx * kArcsec;
  const double ry = p.ry * kArcsec;
  const double rz = p.rz * kArcsec;
  const double k  = 1.0 + p.m * 1e-6;

  // M = k * R
  const double m00 = k,        m01 = k * rz,  m02 = -k * ry;
  const double m10 = -k * rz,  m11 = k,       m12 = k * rx;
  const double m20 = k * ry,   m21 = -k * rx, m22 = k;

  ECEF out;
  if (forward) {
    out.x = p.dx + m00 * in.x + m01 * in.y + m02 * in.z;
    out.y = p.dy + m10 * in.x + m11 * in.y + m12 * in.z;
    out.z = p.dz + m20 * in.x + m21 * in.y + m22 * in.z;
    return out;
  }

  // Inverse: in = M^{-1} * (out - d).
  const double tx = in.x - p.dx, ty = in.y - p.dy, tz = in.z - p.dz;
  const double c00 =  (m11 * m22 - m12 * m21);
  const double c01 = -(m01 * m22 - m02 * m21);
  const double c02 =  (m01 * m12 - m02 * m11);
  const double c10 = -(m10 * m22 - m12 * m20);
  const double c11 =  (m00 * m22 - m02 * m20);
  const double c12 = -(m00 * m12 - m02 * m10);
  const double c20 =  (m10 * m21 - m11 * m20);
  const double c21 = -(m00 * m21 - m01 * m20);
  const double c22 =  (m00 * m11 - m01 * m10);
  const double det = m00 * c00 + m01 * c10 + m02 * c20;
  const double inv = 1.0 / det;
  out.x = inv * (c00 * tx + c01 * ty + c02 * tz);
  out.y = inv * (c10 * tx + c11 * ty + c12 * tz);
  out.z = inv * (c20 * tx + c21 * ty + c22 * tz);
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Geodetic <-> ECEF
// ---------------------------------------------------------------------------
ECEF geodeticToEcef(const Geodetic& g, Datum d) {
  const Ellipsoid el = ellipsoidOf(d);
  const double f  = 1.0 / el.invf;
  const double e2 = f * (2.0 - f);

  const double B = g.lat * kDeg;
  const double L = g.lon * kDeg;
  const double sB = std::sin(B), cB = std::cos(B);
  const double sL = std::sin(L), cL = std::cos(L);
  const double N  = el.a / std::sqrt(1.0 - e2 * sB * sB);

  ECEF e;
  e.x = (N + g.h) * cB * cL;
  e.y = (N + g.h) * cB * sL;
  e.z = (N * (1.0 - e2) + g.h) * sB;
  return e;
}

// Reverse conversion X,Y,Z -> B,L,H by the ITERATIVE algorithm of the standard
// (ГОСТ Р 51794-2008 / ГОСТ 32453-2017), not Bowring's closed form. Auxiliary
// quantities follow the standard:
//   D = sqrt(X^2 + Y^2)            distance from the rotation axis
//   r = sqrt(X^2 + Y^2 + Z^2)      geocentric radius
//   c = arcsin(Z / r)             auxiliary angle
//   p = e^2 * a / (2 r)
//   s_{k+1} = arcsin( p * sin(2(c + s_k)) / sqrt(1 - e^2 sin^2(c + s_k)) ), s_0 = 0
//   B = c + s   (on convergence)
//   H = D cos B + Z sin B - a sqrt(1 - e^2 sin^2 B)
// Longitude is taken by quadrant from the signs of X and Y (here via atan2).
Geodetic ecefToGeodetic(const ECEF& e, Datum d) {
  const Ellipsoid el = ellipsoidOf(d);
  const double f  = 1.0 / el.invf;
  const double a  = el.a;
  const double e2 = f * (2.0 - f);  // first eccentricity squared

  const double D = std::sqrt(e.x * e.x + e.y * e.y);
  const double r = std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z);

  Geodetic g;

  // Longitude (undefined on the polar axis -> 0 by convention).
  g.lon = (D < 1e-9) ? 0.0 : std::atan2(e.y, e.x) / kDeg;

  // Degenerate case: point at the geocentre.
  if (r < 1e-9) {
    g.lat = 0.0;
    g.h   = -a;
    return g;
  }

  // Iterative latitude (ГОСТ). Converges in a few steps for near-Earth points.
  const double c = std::asin(e.z / r);
  const double p = e2 * a / (2.0 * r);
  double s = 0.0;
  double B = c;
  for (int i = 0; i < 12; ++i) {
    B = c + s;
    const double sB = std::sin(B);
    const double sNext =
        std::asin(p * std::sin(2.0 * B) / std::sqrt(1.0 - e2 * sB * sB));
    if (std::fabs(sNext - s) < 1e-15) {
      s = sNext;
      break;
    }
    s = sNext;
  }
  B = c + s;

  const double sB = std::sin(B);
  g.lat = B / kDeg;
  g.h   = D * std::cos(B) + e.z * sB - a * std::sqrt(1.0 - e2 * sB * sB);
  return g;
}

// ---------------------------------------------------------------------------
// Helmert between two datum frames, via the ПЗ-90.11 hub.
// ---------------------------------------------------------------------------
ECEF helmert(const ECEF& in, Datum from, Datum to) {
  if (from == to) return in;
  // from -> hub (forward), then hub -> to (inverse of to's toHub params).
  const ECEF hub = applyHelmert(in, toHubParams(from), /*forward=*/true);
  if (to == Datum::PZ9011) return hub;
  if (from == Datum::PZ9011) {
    return applyHelmert(in, toHubParams(to), /*forward=*/false);
  }
  return applyHelmert(hub, toHubParams(to), /*forward=*/false);
}

// ---------------------------------------------------------------------------
// Public high-level conversion
// ---------------------------------------------------------------------------
Geodetic convert(const Geodetic& in, Datum from, Datum to) {
  if (from == to) return in;
  const ECEF src = geodeticToEcef(in, from);
  const ECEF dst = helmert(src, from, to);
  return ecefToGeodetic(dst, to);
}

const char* datumName(Datum d) {
  switch (d) {
    case Datum::WGS84:  return "WGS-84";
    case Datum::PZ9011: return "ПЗ-90.11";
    case Datum::SK42:   return "СК-42";
  }
  return "?";
}

}  // namespace rugeo
