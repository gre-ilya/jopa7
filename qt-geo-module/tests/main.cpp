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
#include <QFile>
#include <QString>
#include <QTemporaryDir>

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

  // ---- save(): build a GeoData in memory, write it to every supported
  // format, read it back and verify the round trip (incl. Cyrillic text).
  {
    geo::GeoData src;
    geo::GeoPoint a;
    a.name = QString::fromUtf8("Точка Москва");
    a.description = QString::fromUtf8("Красная площадь");
    a.latitude = 55.7558;
    a.longitude = 37.6173;
    a.altitude = 150.0;
    a.hasAltitude = true;
    geo::GeoPoint b;
    b.name = QString::fromUtf8("Питер");
    b.latitude = 59.9391;
    b.longitude = 30.3159;
    src.points = {a, b};
    geo::GeoRoute r;
    r.name = QString::fromUtf8("Маршрут №1");
    r.points = {0, 1, 0};
    src.routes = {r};

    QTemporaryDir tmp;
    check(tmp.isValid(), QStringLiteral("temporary dir for save() tests"));
    for (const QString& ext : geo::GeoFileParser::supportedSaveExtensions()) {
      const QString out = tmp.filePath(QStringLiteral("out.") + ext);
      QString serr;
      const bool saved = parser.save(out, src, &serr);
      check(saved, QStringLiteral("save %1 (%2)").arg(out, serr));
      if (!saved) {
        continue;
      }
      geo::GeoData back;
      const bool reread = parser.parse(out, back, &serr);
      check(reread, QStringLiteral("re-parse %1 (%2)").arg(out, serr));
      if (!reread) {
        continue;
      }
      check(back.points.size() >= 2,
            QStringLiteral(".%1: at least the 2 waypoints survive").arg(ext));
      bool cyr = false;
      double dlat = 1.0;
      for (const geo::GeoPoint& p : back.points) {
        if (p.name == a.name) {
          cyr = true;
          dlat = qAbs(p.latitude - a.latitude);
        }
      }
      check(cyr, QStringLiteral(".%1: Cyrillic point name survives").arg(ext));
      // GDB stores coordinates as 32-bit semicircles (~3 mm quantization).
      check(dlat < 1e-6, QStringLiteral(".%1: latitude round-trips").arg(ext));
      // GeoJSON writes the route as a LineString, which its reader maps
      // back onto a route (only MultiLineString becomes a track), so the
      // route survives for every format even though tracks are ignored.
      check(!back.routes.isEmpty() && back.routes[0].points.size() == 3,
            QStringLiteral(".%1: route with 3 points survives").arg(ext));
    }

    // ---- Tracks are ignored on parse: a GPX with one waypoint and a
    // 3-point track must yield exactly one point and no routes.
    {
      const QString trkIn = tmp.filePath(QStringLiteral("trk_in.gpx"));
      QFile f(trkIn);
      check(f.open(QIODevice::WriteOnly), QStringLiteral("tracks: write input"));
      f.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<gpx version=\"1.0\" creator=\"t\">\n"
              "<wpt lat=\"55.7\" lon=\"37.6\"><name>WPT1</name></wpt>\n"
              "<trk><name>T</name><trkseg>\n"
              "<trkpt lat=\"55.0\" lon=\"37.0\"/>"
              "<trkpt lat=\"55.1\" lon=\"37.1\"/>"
              "<trkpt lat=\"55.2\" lon=\"37.2\"/>\n"
              "</trkseg></trk></gpx>\n");
      f.close();
      geo::GeoData back;
      QString serr;
      check(parser.parse(trkIn, back, &serr),
            QStringLiteral("tracks: parse (%1)").arg(serr));
      check(back.points.size() == 1 &&
                back.points[0].name == QLatin1String("WPT1"),
            QStringLiteral("tracks: only the waypoint is parsed (%1 pts)")
                .arg(back.points.size()));
      check(back.routes.isEmpty(),
            QStringLiteral("tracks: no routes from the track"));
    }

    // ---- Point creation time: read from GPX, written back to GPX and GDB.
    {
      const QString tIn = tmp.filePath(QStringLiteral("time_in.gpx"));
      QFile f(tIn);
      check(f.open(QIODevice::WriteOnly), QStringLiteral("time: write input"));
      f.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<gpx version=\"1.0\" creator=\"t\">\n"
              "<wpt lat=\"55.7\" lon=\"37.6\"><time>2020-05-01T10:20:30Z</time>"
              "<name>Timed</name></wpt>\n"
              "<wpt lat=\"55.8\" lon=\"37.7\"><name>NoTime</name></wpt>\n"
              "</gpx>\n");
      f.close();

      const QDateTime expected =
          QDateTime::fromString(QStringLiteral("2020-05-01T10:20:30Z"), Qt::ISODate);
      geo::GeoData d;
      QString serr;
      check(parser.parse(tIn, d, &serr),
            QStringLiteral("time: parse (%1)").arg(serr));
      QDateTime got;
      bool noTimeIsInvalid = false;
      for (const geo::GeoPoint& p : d.points) {
        if (p.name == QLatin1String("Timed")) {
          got = p.time;
        }
        if (p.name == QLatin1String("NoTime")) {
          noTimeIsInvalid = !p.time.isValid();
        }
      }
      check(got.isValid() && got.toUTC() == expected,
            QStringLiteral("time: parsed from GPX (%1)").arg(got.toString(Qt::ISODate)));
      check(noTimeIsInvalid,
            QStringLiteral("time: point without <time> stays invalid"));

      // GPX -> GPX
      const QString tOutGpx = tmp.filePath(QStringLiteral("time_out.gpx"));
      check(parser.save(tOutGpx, d, &serr),
            QStringLiteral("time: save gpx (%1)").arg(serr));
      QFile fo(tOutGpx);
      fo.open(QIODevice::ReadOnly);
      const QString gpxText = QString::fromUtf8(fo.readAll());
      check(gpxText.contains(QLatin1String("2020-05-01T10:20:30Z")),
            QStringLiteral("time: written to GPX"));

      // GPX -> GDB -> parse (GDB stores whole seconds)
      const QString tOutGdb = tmp.filePath(QStringLiteral("time_out.gdb"));
      check(parser.save(tOutGdb, d, &serr),
            QStringLiteral("time: save gdb (%1)").arg(serr));
      geo::GeoData back;
      check(parser.parse(tOutGdb, back, &serr),
            QStringLiteral("time: re-parse gdb (%1)").arg(serr));
      QDateTime gdbGot;
      for (const geo::GeoPoint& p : back.points) {
        if (p.name == QLatin1String("Timed")) {
          gdbGot = p.time;
        }
      }
      check(gdbGot.isValid() &&
                gdbGot.toSecsSinceEpoch() == expected.toSecsSinceEpoch(),
            QStringLiteral("time: survives GDB round trip"));
    }

    // ---- GDB version 2 (legacy MapSource, CP1251 strings).
    {
      const QString out2 = tmp.filePath(QStringLiteral("out_v2.gdb"));
      QString serr;
      geo::SaveOptions opts;
      opts.gdbVersion = 2;
      check(parser.save(out2, src, opts, &serr),
            QStringLiteral("save GDB v2 (%1)").arg(serr));

      // The header must carry the v2 letter: "MsRcf\0" + reclen + 'D' + 'l'.
      QFile f(out2);
      check(f.open(QIODevice::ReadOnly), QStringLiteral("v2: open for header"));
      const QByteArray head = f.read(12);
      check(head.size() == 12 && head.at(10) == 'D' && head.at(11) == 'l',
            QStringLiteral("v2: header version letter is 'l' (GDB v2)"));

      // Round trip: parse() re-decodes v1/v2 strings from CP1251, so the
      // Cyrillic names written by the CP1251 encoder must come back intact.
      geo::GeoData back;
      check(parser.parse(out2, back, &serr),
            QStringLiteral("v2: re-parse (%1)").arg(serr));
      bool cyr = false;
      for (const geo::GeoPoint& p : back.points) {
        if (p.name == a.name && p.description == a.description) {
          cyr = true;
        }
      }
      check(cyr, QStringLiteral("v2: Cyrillic name+description survive CP1251"));
      check(!back.routes.isEmpty() &&
                back.routes[0].name == r.name &&
                back.routes[0].points.size() == 3,
            QStringLiteral("v2: Cyrillic route with 3 points survives"));

      // An unsupported version must be refused up front.
      check(!parser.save(out2, src, geo::SaveOptions{7}, &serr),
            QStringLiteral("v2: version 7 rejected (%1)").arg(serr));
    }
  }

  printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n",
         failures);
  return failures == 0 ? 0 : 1;
}
