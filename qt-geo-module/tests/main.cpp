// Console self-test for geo::GeoFileParser.
//
// Parses tests/data/sample.gdb and verifies the expected points and routes,
// including that a point used twice in a route collapses to one shared index
// that simply repeats.  Returns non-zero on any failure so it can gate CI.
//
// No GUI / display required (uses QCoreApplication), so it runs on Linux,
// Windows and macOS CI runners alike.

#include <cstdio>

#include <QCoreApplication>
#include <QDir>
#include <QString>

#include "geofile.h"

static int failures = 0;

static void check(bool cond, const QString& what)
{
  if (cond) {
    printf("  ok   - %s\n", qPrintable(what));
  } else {
    printf("  FAIL - %s\n", qPrintable(what));
    ++failures;
  }
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);

  // The sample lives next to the source; allow an override via argv[1] so the
  // build system can point at it explicitly.
  QString path = (argc > 1)
      ? QString::fromLocal8Bit(argv[1])
      : QStringLiteral(GEO_TEST_DATA_DIR) + QStringLiteral("/sample.gdb");

  printf("Parsing: %s\n", qPrintable(path));

  geo::GeoFileParser parser;
  geo::GeoData data;
  QString error;
  if (!parser.parse(path, data, &error)) {
    printf("  FAIL - parse() returned false: %s\n", qPrintable(error));
    return 1;
  }

  // sample.gdb has 4 waypoints and 2 routes (see README / tests/data).
  check(data.points.size() == 4, QStringLiteral("4 points"));
  check(data.routes.size() == 2, QStringLiteral("2 routes"));

  if (data.points.size() == 4) {
    check(data.points[0].name == QLatin1String("MOSCOW"), QStringLiteral("point 0 == MOSCOW"));
    check(data.points[0].hasAltitude, QStringLiteral("point 0 has altitude"));
    check(qAbs(data.points[0].latitude - 55.751244) < 1e-4,
          QStringLiteral("point 0 latitude ~ 55.751244"));
  }

  if (data.routes.size() == 2) {
    // WEST_LOOP: MOSCOW(0) -> SPB(1) -> MOSCOW(0): the repeated point is the
    // same shared index appearing twice.
    const geo::GeoRoute& r0 = data.routes[0];
    check(r0.points == (QVector<int>{0, 1, 0}),
          QStringLiteral("route 0 indices == [0,1,0] (point repeats)"));
    // EAST_TRIP: MOSCOW(0) -> NNOVGOROD(2) -> EKB(3).
    const geo::GeoRoute& r1 = data.routes[1];
    check(r1.points == (QVector<int>{0, 2, 3}),
          QStringLiteral("route 1 indices == [0,2,3]"));
  }

  // A bogus file must fail gracefully (no crash, returns false).
  geo::GeoData dummy;
  QString err2;
  check(!parser.parse(QStringLiteral("/no/such/file.gdb"), dummy, &err2),
        QStringLiteral("missing file fails gracefully"));

  printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n",
         failures);
  return failures == 0 ? 0 : 1;
}
