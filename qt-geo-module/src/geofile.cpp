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
  p.name = wpt->shortname;
  // Prefer description; fall back to notes when description is empty.
  p.description = !wpt->description.isEmpty() ? wpt->description : wpt->notes;
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
    route.name = rte->rte_name;
    route.description = rte->rte_desc;
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
