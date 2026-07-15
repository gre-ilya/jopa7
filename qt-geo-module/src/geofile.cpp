/*
    geofile.cpp -- implementation of the GPSBabel-based geo parsing module.

    Copyright (C) 2024 (module wrapper)
    Built on top of GPSBabel, Copyright (C) Robert Lipe and contributors.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include "geofile.h"

#include <clocale>          // for setlocale, LC_NUMERIC, LC_TIME
#include <functional>       // for std::function
#include <memory>           // for std::unique_ptr

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

// GPSBabel internals.  These live in the GPSBabel source tree; geobabel.pri
// adds that directory to the include path.
#include "defs.h"                     // global_opts, Waypoint, route_head, list helpers
#include "format.h"                   // Format base class
#include "gdb.h"                      // GdbFormat
#include "session.h"                  // session_init, start_session
#include "src/core/datetime.h"        // gpsbabel::DateTime
#include "src/core/usasciicodec.h"    // gpsbabel::UsAsciiCodec

// GPSBabel's global lists.  defs.h only declares these inside template bodies,
// so we (re)declare them here at global scope to iterate over the parse result.
extern WaypointList* global_waypoint_list;
extern RouteList* global_route_list;

namespace geo {

namespace {

// GPSBabel keeps a lot of process-global state and is not re-entrant, so all
// parsing is serialized.
QMutex g_parseMutex;

// One-time GPSBabel runtime initialization (mirrors the relevant parts of
// GPSBabel's main()).  Must run once before any format reader is used.
void ensureGlobalInit()
{
  static bool initialized = false;
  if (initialized) {
    return;
  }

  // GPSBabel parses/formats numbers and times in the "C" locale.
  setlocale(LC_NUMERIC, "C");
  setlocale(LC_TIME, "C");

  // Make a US-ASCII codec available (GPSBabel registers it globally).
  static gpsbabel::UsAsciiCodec* codec = new gpsbabel::UsAsciiCodec();
  (void)codec;

  global_opts.objective = wptdata;
  global_opts.masked_objective = NOTHINGMASK;
  global_opts.inifile = nullptr;

  gpsbabel_time = current_time().toTime_t();

  session_init();
  waypt_init();
  route_init();

  initialized = true;
}

// Discard any data left in the global lists from a previous parse.
void clearGlobalLists()
{
  route_flush_all_routes();
  route_flush_all_tracks();
  waypt_flush_all();
}

// --- GDB v1/v2 Cyrillic fix -------------------------------------------------
// GDB v1/v2 store strings in the writer's ANSI codepage (CP1251 on Russian
// Windows), but GPSBabel's GdbFormat decodes them with QString::fromLatin1(),
// which garbles Cyrillic ("Òî÷êà").  fromLatin1 maps bytes 1:1 onto
// U+0000..U+00FF, so the original bytes are recoverable: we sniff the GDB
// version from the file header and, for v1/v2, re-decode all strings from
// CP1251.  GDB v3 stores UTF-8 and needs no fixing.

// Set per parse() call; parsing is serialized by g_parseMutex, so a file-scope
// flag is safe.
bool g_reencodeCp1251 = false;

// Returns 1, 2 or 3; -1 if the header does not look like a GDB file.
// Layout: "MsRcf\0", int32 record length, then 'D' + version letter
// ('k' = v1, 'l' = v2, 'm' = v3) -- same detection as GdbFormat (gdb.cc:398).
int sniffGdbVersion(const QString& filePath)
{
  QFile f(filePath);
  if (!f.open(QIODevice::ReadOnly)) {
    return -1;
  }
  const QByteArray head = f.read(64);
  const int idx = head.indexOf("MsRcf");
  if (idx < 0) {
    return -1;
  }
  const int p = idx + 6 + 4;  // skip "MsRcf\0" + int32 record length
  if (p + 1 >= head.size() || head.at(p) != 'D') {
    return -1;
  }
  const char v = head.at(p + 1);
  if (v < 'k' || v > 'm') {
    return -1;
  }
  return v - 'k' + 1;
}

// Windows-1251 -> Unicode for the high half (0x80..0xFF).  Kept inline so the
// module stays Qt5/Qt6 portable without QTextCodec / Qt5Compat.
QString cp1251ToUnicode(const QByteArray& bytes)
{
  static const char16_t kHigh[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
  };
  QString out;
  out.reserve(bytes.size());
  for (const char c : bytes) {
    const auto u = static_cast<unsigned char>(c);
    out += (u < 0x80) ? QChar(u) : QChar(kHigh[u - 0x80]);
  }
  return out;
}

QString fixEncoding(const QString& s)
{
  if (!g_reencodeCp1251 || s.isEmpty()) {
    return s;
  }
  // Undo GPSBabel's fromLatin1 (lossless byte round-trip), re-decode as 1251.
  return cp1251ToUnicode(s.toLatin1());
}
// ----------------------------------------------------------------------------

// Build a stable key so that route points can be matched back to the
// standalone waypoint they reference (GPSBabel stores route points as copies).
QString pointKey(const QString& name, double lat, double lon)
{
  return QStringLiteral("%1|%2|%3")
      .arg(name)
      .arg(lat, 0, 'f', 7)
      .arg(lon, 0, 'f', 7);
}

GeoPoint toGeoPoint(const Waypoint* wpt)
{
  GeoPoint p;
  p.name = fixEncoding(wpt->shortname);
  // Prefer description; fall back to notes when description is empty.
  p.description =
      fixEncoding(!wpt->description.isEmpty() ? wpt->description : wpt->notes);
  p.latitude = wpt->latitude;
  p.longitude = wpt->longitude;
  if (wpt->altitude != unknown_alt) {
    p.altitude = wpt->altitude;
    p.hasAltitude = true;
  }
  return p;
}

// Create the right reader for a given extension.  Extend this map to support
// more GPSBabel formats (each one is just another Format subclass).
std::unique_ptr<Format> makeReader(const QString& ext)
{
  if (ext == QLatin1String("gdb")) {
    return std::make_unique<GdbFormat>();
  }
  return nullptr;
}

} // namespace

QStringList GeoFileParser::supportedExtensions()
{
  // Keep in sync with makeReader().
  return {QStringLiteral("gdb")};
}

bool GeoFileParser::isSupported(const QString& filePath)
{
  const QString ext = QFileInfo(filePath).suffix().toLower();
  return supportedExtensions().contains(ext);
}

bool GeoFileParser::parse(const QString& filePath, GeoData& out, QString* errorMessage)
{
  auto fail = [&](const QString& msg) {
    if (errorMessage) {
      *errorMessage = msg;
    }
    return false;
  };

  const QFileInfo fi(filePath);
  if (!fi.exists()) {
    return fail(QStringLiteral("File does not exist: %1").arg(filePath));
  }

  const QString ext = fi.suffix().toLower();
  std::unique_ptr<Format> reader = makeReader(ext);
  if (!reader) {
    return fail(QStringLiteral("Unsupported file format: .%1").arg(ext));
  }

  QMutexLocker locker(&g_parseMutex);

  // GDB v1/v2 need the CP1251 re-decode (see fixEncoding above); v3 is UTF-8.
  const int gdbVer = (ext == QLatin1String("gdb")) ? sniffGdbVersion(filePath) : -1;
  g_reencodeCp1251 = (gdbVer == 1 || gdbVer == 2);

  ensureGlobalInit();
  clearGlobalLists();

  out = GeoData();

  // GPSBabel reports unrecoverable problems with fatal(), which throws a
  // gpsbabel::Fatal (a QString-derived exception).
  try {
    start_session(ext, filePath);
    reader->rd_init(filePath);
    reader->read();
    reader->rd_deinit();
  } catch (const QString& e) {
    clearGlobalLists();
    return fail(e);
  } catch (const std::exception& e) {
    clearGlobalLists();
    return fail(QString::fromUtf8(e.what()));
  }

  // Index of points already added, so route points can reference the same
  // shared point and so a point used several times collapses to one entry.
  QHash<QString, int> indexByKey;

  auto addPoint = [&](const Waypoint* wpt) -> int {
    const QString key = pointKey(wpt->shortname, wpt->latitude, wpt->longitude);
    auto it = indexByKey.constFind(key);
    if (it != indexByKey.constEnd()) {
      return it.value();
    }
    const int idx = out.points.size();
    out.points.append(toGeoPoint(wpt));
    indexByKey.insert(key, idx);
    return idx;
  };

  // 1. Standalone waypoints first, so routes prefer to reference them.
  for (const Waypoint* wpt : *global_waypoint_list) {
    addPoint(wpt);
  }

  // 2. Routes, mapping each route point onto a shared point index.
  for (const route_head* rte : *global_route_list) {
    GeoRoute route;
    route.name = fixEncoding(rte->rte_name);
    route.description = fixEncoding(rte->rte_desc);
    for (const Waypoint* wpt : rte->waypoint_list) {
      route.points.append(addPoint(wpt));
    }
    out.routes.append(route);
  }

  // Leave the global lists empty for the next caller.
  clearGlobalLists();

  return true;
}

} // namespace geo
