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
#include <QTemporaryDir>

#include "geofile.h"
#include "geodocument.h"

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
      check(!back.routes.isEmpty() && back.routes[0].points.size() == 3,
            QStringLiteral(".%1: route with 3 points survives").arg(ext));
    }
  }

  // ---- GeoDocument: open-with-lock / edit / save / close.
  {
    QTemporaryDir tmp;
    check(tmp.isValid(), QStringLiteral("temporary dir for GeoDocument tests"));
    const QString docPath = tmp.filePath(QStringLiteral("doc.gpx"));

    // Prepare a document on disk.
    {
      geo::GeoData src;
      geo::GeoPoint p;
      p.name = QString::fromUtf8("База");
      p.latitude = 55.0;
      p.longitude = 37.0;
      src.points = {p};
      QString serr;
      check(parser.save(docPath, src, &serr),
            QStringLiteral("GeoDocument: prepare file (%1)").arg(serr));
    }

    geo::GeoDocument doc1;
    QString derr;
    check(doc1.open(docPath, &derr),
          QStringLiteral("GeoDocument: first open succeeds (%1)").arg(derr));
    check(doc1.isOpen() && doc1.filePath() == docPath,
          QStringLiteral("GeoDocument: isOpen/filePath"));

    // A second document (second app copy) must be refused while locked.
    geo::GeoDocument doc2;
    QString berr;
    check(!doc2.open(docPath, &berr),
          QStringLiteral("GeoDocument: second open refused (%1)").arg(berr));

    // Edit in memory, save, close.
    doc1.data().points[0].name = QString::fromUtf8("База-2");
    check(doc1.save(&derr),
          QStringLiteral("GeoDocument: save (%1)").arg(derr));
    doc1.close();
    check(!doc1.isOpen(), QStringLiteral("GeoDocument: closed"));

    // After close the file is free again and carries the edit.
    check(doc2.open(docPath, &berr),
          QStringLiteral("GeoDocument: open after close (%1)").arg(berr));
    check(!doc2.data().points.isEmpty() &&
              doc2.data().points[0].name == QString::fromUtf8("База-2"),
          QStringLiteral("GeoDocument: edit persisted"));

    // saveAs moves the document (and the lock) to a new path.
    const QString docPath2 = tmp.filePath(QStringLiteral("doc2.gpx"));
    check(doc2.saveAs(docPath2, &berr),
          QStringLiteral("GeoDocument: saveAs (%1)").arg(berr));
    check(doc2.filePath() == docPath2,
          QStringLiteral("GeoDocument: filePath follows saveAs"));
    geo::GeoDocument doc3;
    check(doc3.open(docPath, &berr),
          QStringLiteral("GeoDocument: old path unlocked after saveAs"));
    geo::GeoDocument doc4;
    check(!doc4.open(docPath2, &berr),
          QStringLiteral("GeoDocument: new path locked by saveAs owner (%1)")
              .arg(berr));
  }

  printf(failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n",
         failures);
  return failures == 0 ? 0 : 1;
}
