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
#include <vector>           // for std::vector (route payload)

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
#include "gpx.h"                      // GpxFormat
#include "geojson.h"                  // GeoJsonFormat
#include "session.h"                  // session_init, start_session
#include "src/core/datetime.h"        // gpsbabel::DateTime
#include "src/core/usasciicodec.h"    // gpsbabel::UsAsciiCodec

// GPSBabel's global lists.  defs.h only declares these inside template bodies,
// so we (re)declare them here at global scope to iterate over the parse result.
extern WaypointList* global_waypoint_list;
extern RouteList* global_route_list;
extern RouteList* global_track_list;

namespace geo {

// --- Full-fidelity payloads (opaque in the public header) -------------------
// Complete copies of what GPSBabel parsed.  Waypoint's copy constructor deep
// copies the format-specific chain (garmin_fs with icons/categories/ilinks,
// fs_xml with GPX extensions), so one Waypoint copy carries everything.
namespace detail {

class PointPayload {
public:
  explicit PointPayload(const Waypoint& w) : wpt(w) {}
  Waypoint wpt;
};

class RoutePayload {
public:
  RoutePayload() = default;
  RoutePayload(const RoutePayload&) = delete;
  RoutePayload& operator=(const RoutePayload&) = delete;
  ~RoutePayload() { fs.FsChainDestroy(); }  // manual chain ownership

  std::vector<Waypoint> wpts;  // the line's own point copies, in order
  UrlList urls;
  int rteNum = 0;
  FormatSpecificDataList fs;   // deep copy (FsChainCopy)
  gb_color lineColor;
  int lineWidth = -1;
};

}  // namespace detail
// ----------------------------------------------------------------------------

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
// Windows-1251 upper half (0x80..0xFF) -> Unicode.
static const char16_t kCp1251High[128] = {
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

QString cp1251ToUnicode(const QByteArray& bytes)
{
  QString out;
  out.reserve(bytes.size());
  for (const char c : bytes) {
    const auto u = static_cast<unsigned char>(c);
    out += (u < 0x80) ? QChar(u) : QChar(kCp1251High[u - 0x80]);
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

// The reverse of fixEncoding, for WRITING legacy GDB v1/v2: GPSBabel encodes
// v1/v2 strings with toLatin1(), which turns Cyrillic into '?'.  Map each
// character to its Windows-1251 byte and present that byte as the Latin-1
// character U+00xx, so toLatin1() ends up emitting the correct CP1251 bytes
// (exactly what a Russian MapSource writes).  Unmappable characters become '?'.
QString unicodeToCp1251(const QString& s)
{
  static const QHash<char16_t, uchar> kReverse = [] {
    QHash<char16_t, uchar> m;
    for (int i = 0; i < 128; ++i) {
      m.insert(kCp1251High[i], static_cast<uchar>(0x80 + i));
    }
    return m;
  }();
  QString out;
  out.reserve(s.size());
  for (const QChar c : s) {
    const char16_t u = c.unicode();
    if (u < 0x80) {
      out += c;
    } else {
      out += QChar(kReverse.value(u, uchar('?')));
    }
  }
  return out;
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
  // Full-fidelity copy; visible strings normalized the same way as above so
  // the projection and the payload stay directly comparable.
  auto payload = std::make_shared<detail::PointPayload>(*wpt);
  if (g_reencodeCp1251) {
    payload->wpt.shortname = fixEncoding(payload->wpt.shortname);
    payload->wpt.description = fixEncoding(payload->wpt.description);
    payload->wpt.notes = fixEncoding(payload->wpt.notes);
  }
  p.payload = std::move(payload);
  return p;
}

// Capture the full-fidelity payload of one route/track head.
std::shared_ptr<const detail::RoutePayload> captureRoutePayload(
    const route_head* rte)
{
  auto payload = std::make_shared<detail::RoutePayload>();
  payload->urls = rte->rte_urls;
  payload->rteNum = rte->rte_num;
  payload->fs = rte->fs.FsChainCopy();
  payload->lineColor = rte->line_color;
  payload->lineWidth = rte->line_width;
  payload->wpts.reserve(rte->rte_waypt_ct());
  for (const Waypoint* wpt : rte->waypoint_list) {
    payload->wpts.emplace_back(*wpt);
    if (g_reencodeCp1251) {
      Waypoint& w = payload->wpts.back();
      w.shortname = fixEncoding(w.shortname);
      w.description = fixEncoding(w.description);
      w.notes = fixEncoding(w.notes);
    }
  }
  return payload;
}

// Create the right reader for a given extension.  Extend this map to support
// more GPSBabel formats (each one is just another Format subclass).
std::unique_ptr<Format> makeReader(const QString& ext)
{
  if (ext == QLatin1String("gdb")) {
    return std::make_unique<GdbFormat>();
  }
  if (ext == QLatin1String("gpx")) {
    return std::make_unique<GpxFormat>();
  }
  if (ext == QLatin1String("geojson") || ext == QLatin1String("json")) {
    return std::make_unique<GeoJsonFormat>();
  }
  return nullptr;
}

// Create the right writer for a given extension.  Same Format subclasses:
// every format included in this module implements both reading and writing.
std::unique_ptr<Format> makeWriter(const QString& ext)
{
  return makeReader(ext);
}

// Set a format option (the equivalent of gpsbabel's -o fmt,opt=value).  The
// option table maps option names onto char* members of the Format subclass.
// The value must outlive the format object; pass a string literal.
void setFormatOption(Format* fmt, const QString& name, const char* value)
{
  QVector<arglist_t>* args = fmt->get_args();
  if (!args) {
    return;
  }
  for (arglist_t& a : *args) {
    if (a.argval && a.argstring.compare(name, Qt::CaseInsensitive) == 0) {
      *a.argval = const_cast<char*>(value);
      return;
    }
  }
}

// gpsbabel's vecs machinery normally seeds every format option with its
// declared default before the format runs; formats rely on that (e.g. the GPX
// writer does xstrtoi(snlen) without a null check).  This wrapper bypasses
// vecs, so apply the defaults ourselves.  Returns the strings we allocated;
// free them with xfree() once the format is done.
QVector<char*> applyOptionDefaults(Format* fmt)
{
  QVector<char*> owned;
  if (QVector<arglist_t>* args = fmt->get_args()) {
    for (arglist_t& a : *args) {
      if (a.argval && *a.argval == nullptr && !a.defaultvalue.isEmpty()) {
        char* v = xstrdup(a.defaultvalue);
        *a.argval = v;
        owned.append(v);
      }
    }
  }
  return owned;
}

} // namespace

QStringList GeoFileParser::supportedExtensions()
{
  // Keep in sync with makeReader().
  return {QStringLiteral("gdb"), QStringLiteral("gpx"),
          QStringLiteral("geojson"), QStringLiteral("json")};
}

QStringList GeoFileParser::supportedSaveExtensions()
{
  // Keep in sync with makeWriter().
  return supportedExtensions();
}

bool GeoFileParser::isSupported(const QString& filePath)
{
  const QString ext = QFileInfo(filePath).suffix().toLower();
  return supportedExtensions().contains(ext);
}

bool GeoFileParser::parse(const QString& filePath, GeoData& out, QString* errorMessage)
{
  return parse(filePath, out, ParseOptions(), errorMessage);
}

bool GeoFileParser::parse(const QString& filePath, GeoData& out,
                          const ParseOptions& options, QString* errorMessage)
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
  const QVector<char*> ownedOpts = applyOptionDefaults(reader.get());
  auto freeOwnedOpts = [&ownedOpts]() {
    for (char* v : ownedOpts) {
      xfree(v);
    }
  };
  try {
    start_session(ext, filePath);
    reader->rd_init(filePath);
    reader->read();
    reader->rd_deinit();
  } catch (const QString& e) {
    clearGlobalLists();
    freeOwnedOpts();
    return fail(e);
  } catch (const std::exception& e) {
    clearGlobalLists();
    freeOwnedOpts();
    return fail(QString::fromUtf8(e.what()));
  }
  freeOwnedOpts();

  // Index of points already added, so route points can reference the same
  // shared point and so a point used several times collapses to one entry.
  QHash<QString, int> indexByKey;

  auto addPoint = [&](const Waypoint* wpt, bool standalone) -> int {
    const QString key = pointKey(wpt->shortname, wpt->latitude, wpt->longitude);
    auto it = indexByKey.constFind(key);
    if (it != indexByKey.constEnd()) {
      return it.value();
    }
    const int idx = out.points.size();
    GeoPoint p = toGeoPoint(wpt);
    p.standalone = standalone;
    out.points.append(std::move(p));
    indexByKey.insert(key, idx);
    return idx;
  };

  // 1. Standalone waypoints first, so routes prefer to reference them.
  for (const Waypoint* wpt : *global_waypoint_list) {
    addPoint(wpt, /*standalone=*/true);
  }

  // 2. Routes, mapping each route point onto a shared point index.  Points
  // that exist only inside a route are marked non-standalone so that save()
  // does not promote them to file-level waypoints.
  for (const route_head* rte : *global_route_list) {
    GeoRoute route;
    route.name = fixEncoding(rte->rte_name);
    route.description = fixEncoding(rte->rte_desc);
    route.payload = captureRoutePayload(rte);
    for (const Waypoint* wpt : rte->waypoint_list) {
      route.points.append(addPoint(wpt, /*standalone=*/false));
    }
    out.routes.append(route);
  }

  // 3. Tracks, exposed the same way as routes.  Some formats only have this
  // notion for an ordered line: e.g. the GeoJSON reader turns every
  // LineString into a track.  Skipped entirely when the caller opted out
  // (note: data parsed without tracks loses them on a subsequent save()).
  if (options.includeTracks) {
    for (const route_head* trk : *global_track_list) {
      GeoRoute route;
      route.isTrack = true;
      route.name = fixEncoding(trk->rte_name);
      route.description = fixEncoding(trk->rte_desc);
      route.payload = captureRoutePayload(trk);
      for (const Waypoint* wpt : trk->waypoint_list) {
        route.points.append(addPoint(wpt, /*standalone=*/false));
      }
      out.routes.append(route);
    }
  }

  // Leave the global lists empty for the next caller.
  clearGlobalLists();

  return true;
}

bool GeoFileParser::save(const QString& filePath, const GeoData& data,
                         QString* errorMessage)
{
  return save(filePath, data, SaveOptions(), errorMessage);
}

bool GeoFileParser::save(const QString& filePath, const GeoData& data,
                         const SaveOptions& options, QString* errorMessage)
{
  auto fail = [&](const QString& msg) {
    if (errorMessage) {
      *errorMessage = msg;
    }
    return false;
  };

  const QString ext = QFileInfo(filePath).suffix().toLower();
  std::unique_ptr<Format> writer = makeWriter(ext);
  if (!writer) {
    return fail(QStringLiteral("Unsupported output format: .%1").arg(ext));
  }
  const QVector<char*> ownedOpts = applyOptionDefaults(writer.get());
  auto freeOwnedOpts = [&ownedOpts]() {
    for (char* v : ownedOpts) {
      xfree(v);
    }
  };

  // Sanity-check route indices up front, before touching global state.
  for (const GeoRoute& r : data.routes) {
    for (const int idx : r.points) {
      if (idx < 0 || idx >= data.points.size()) {
        return fail(QStringLiteral("Route \"%1\" references point index %2, "
                                   "but there are only %3 points")
                        .arg(r.name).arg(idx).arg(data.points.size()));
      }
    }
  }

  if (ext == QLatin1String("gdb")) {
    if (options.gdbVersion != 2 && options.gdbVersion != 3) {
      return fail(QStringLiteral("Unsupported GDB version %1 (use 2 or 3)")
                      .arg(options.gdbVersion));
    }
    // Without an explicit "ver" the writer would emit an invalid version 0
    // header, because this wrapper bypasses the vecs option machinery that
    // normally applies the default.  v3 stores strings as UTF-8; for v2 the
    // strings are pre-transcoded to CP1251 below.
    setFormatOption(writer.get(), QStringLiteral("ver"),
                    options.gdbVersion == 2 ? "2" : "3");
  }
  // Legacy GDB v1/v2 strings must go out as CP1251 bytes (see unicodeToCp1251).
  const bool cp1251Out =
      (ext == QLatin1String("gdb")) && (options.gdbVersion < 3);
  const auto enc = [cp1251Out](const QString& s) {
    return cp1251Out ? unicodeToCp1251(s) : s;
  };

  QMutexLocker locker(&g_parseMutex);

  ensureGlobalInit();
  clearGlobalLists();
  // waypt_add() records the current session on every waypoint, so a session
  // must be started before populating the lists (same as on the parse path).
  start_session(ext, filePath);

  // Build a full Waypoint for a GeoPoint: start from the fidelity payload
  // when there is one (icons, timestamps, extensions, ... survive), then
  // overlay the editable projection fields.
  const auto buildWaypoint = [&enc](const GeoPoint& p) -> Waypoint* {
    Waypoint* w = p.payload ? new Waypoint(p.payload->wpt) : new Waypoint;
    if (p.payload) {
      // Keep the original description/notes split when the projection was
      // derived from it unchanged (parse() falls back to notes when the
      // description is empty).
      const Waypoint& base = p.payload->wpt;
      const QString projected =
          !base.description.isEmpty() ? base.description : base.notes;
      w->description =
          enc(p.description == projected ? base.description : p.description);
      w->notes = enc(w->notes);
    } else {
      w->description = enc(p.description);
    }
    w->shortname = enc(p.name);
    w->latitude = p.latitude;
    w->longitude = p.longitude;
    w->altitude = p.hasAltitude ? p.altitude : unknown_alt;
    return w;
  };

  // File-level waypoints.  Points that exist only inside a route/track
  // (standalone == false) are written there, not here.
  for (const GeoPoint& p : data.points) {
    if (p.standalone) {
      waypt_add(buildWaypoint(p));
    }
  }
  for (const GeoRoute& r : data.routes) {
    auto* rte = new route_head;
    rte->rte_name = enc(r.name);
    rte->rte_desc = enc(r.description);
    const detail::RoutePayload* rp = r.payload.get();
    if (rp) {
      rte->rte_urls = rp->urls;
      rte->rte_num = rp->rteNum;
      rte->fs = rp->fs.FsChainCopy();  // route_head dtor destroys its chain
      rte->line_color = rp->lineColor;
      rte->line_width = rp->lineWidth;
    }
    // Tracks stay tracks (r.isTrack).  The GeoJSON writer additionally only
    // serializes tracks as LineStrings (it has no route notion), so for that
    // format every line goes to the track list.
    const bool asTrack = r.isTrack ||
        (ext == QLatin1String("geojson") || ext == QLatin1String("json"));
    if (asTrack) {
      track_add_head(rte);
    } else {
      route_add_head(rte);
    }
    // If the line's composition is unchanged, use the positional per-point
    // payload copies: they carry what only exists inside the line (per-point
    // autorouting geometry, trackpoint timestamps/speeds, trkseg splits).
    // Otherwise fall back to the referenced points' own payloads.
    const bool positional =
        rp && static_cast<int>(rp->wpts.size()) == r.points.size();
    for (int i = 0; i < r.points.size(); ++i) {
      const GeoPoint& gp = data.points[r.points[i]];
      Waypoint* w;
      if (positional) {
        const Waypoint& base = rp->wpts[static_cast<size_t>(i)];
        w = new Waypoint(base);
        const QString projected =
            !base.description.isEmpty() ? base.description : base.notes;
        w->shortname = enc(gp.name);
        w->description =
            enc(gp.description == projected ? base.description
                                            : gp.description);
        w->notes = enc(w->notes);
        w->latitude = gp.latitude;
        w->longitude = gp.longitude;
        w->altitude = gp.hasAltitude ? gp.altitude : unknown_alt;
      } else {
        w = buildWaypoint(gp);
      }
      if (asTrack) {
        track_add_wpt(rte, w);
      } else {
        route_add_wpt(rte, w);
      }
    }
  }

  bool ok = true;
  QString error;
  try {
    writer->wr_init(filePath);
    writer->write();
    writer->wr_deinit();
  } catch (const QString& e) {
    ok = false;
    error = e;
  } catch (const std::exception& e) {
    // gb_fatal.cpp turns GPSBabel's fatal() into an exception, so a write
    // error cannot kill the host application.
    ok = false;
    error = QString::fromUtf8(e.what());
  }

  clearGlobalLists();
  freeOwnedOpts();

  if (!ok) {
    return fail(QStringLiteral("Failed to write %1: %2").arg(filePath, error));
  }
  return true;
}

} // namespace geo
