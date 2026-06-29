// ru_geo_test.cpp -- sanity checks and demo for the WGS-84 / ПЗ-90.11 / СК-42
// transforms. Build: see ru_geo.pro, or:
//     g++ -std=c++17 ru_geo.cpp ru_geo_test.cpp -o ru_geo_test && ./ru_geo_test
//
// Exit code 0 = all round-trip checks passed.

#include "ru_geo.h"

#include <cmath>
#include <cstdio>

using namespace rugeo;

static int g_fail = 0;

// Round-trip A->B->A must reproduce the original within tolerance.
static void roundTrip(const char* name, Geodetic p, Datum a, Datum b) {
  Geodetic fwd = convert(p, a, b);
  Geodetic back = convert(fwd, b, a);
  // ~1e-7 deg ≈ 1 cm; height ~ 1 mm.
  double dLat = std::fabs(back.lat - p.lat);
  double dLon = std::fabs(back.lon - p.lon);
  double dH   = std::fabs(back.h   - p.h);
  bool ok = dLat < 1e-11 && dLon < 1e-11 && dH < 1e-6;
  std::printf("  [%s] %-10s %s -> %s -> %s : dLat=%.2e dLon=%.2e dH=%.2e m  %s\n",
              ok ? "OK" : "FAIL", name, datumName(a), datumName(b), datumName(a),
              dLat, dLon, dH, ok ? "" : "<-- MISMATCH");
  if (!ok) ++g_fail;
}

static void show(const char* label, Geodetic from, Datum a, Datum b) {
  Geodetic to = convert(from, a, b);
  std::printf("  %-22s %-9s lat=%.8f lon=%.8f h=%.3f\n"
              "  %-22s %-9s lat=%.8f lon=%.8f h=%.3f  (dN~%.2f m, dE~%.2f m)\n",
              label, datumName(a), from.lat, from.lon, from.h,
              "", datumName(b), to.lat, to.lon, to.h,
              (to.lat - from.lat) * 111320.0,
              (to.lon - from.lon) * 111320.0 * std::cos(from.lat * M_PI / 180.0));
}

int main() {
  // A few test points (lat, lon, h) in degrees/metres.
  Geodetic moscow{55.7558, 37.6173, 150.0};
  Geodetic novosib{55.0084, 82.9357, 120.0};
  Geodetic equator{0.0, 30.0, 0.0};
  Geodetic high{68.9700, 33.0700, 50.0};  // Murmansk-ish (high latitude)

  std::printf("== Round-trip consistency (must all be OK) ==\n");
  Datum all[] = {Datum::WGS84, Datum::PZ9011, Datum::SK42};
  for (Datum a : all)
    for (Datum b : all)
      if (a != b) {
        roundTrip("moscow", moscow, a, b);
        roundTrip("equator", equator, a, b);
        roundTrip("highlat", high, a, b);
      }

  std::printf("\n== Example conversions ==\n");
  show("Moscow",       moscow,  Datum::WGS84,  Datum::SK42);
  show("Moscow",       moscow,  Datum::WGS84,  Datum::PZ9011);
  show("Novosibirsk",  novosib, Datum::SK42,   Datum::WGS84);
  show("Novosibirsk",  novosib, Datum::SK42,   Datum::PZ9011);

  std::printf("\n%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
  return g_fail ? 1 : 0;
}
