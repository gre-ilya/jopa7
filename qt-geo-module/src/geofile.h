/*
    geofile.h -- a small, GPSBabel-based geo-format parsing module.

    This header is intentionally free of any GPSBabel internals so it can be
    included directly from your application code without dragging in the whole
    GPSBabel header tree.  Link against the sources listed in geobabel.pri.

    Copyright (C) 2024 (module wrapper)
    Built on top of GPSBabel, Copyright (C) Robert Lipe and contributors.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
#ifndef GEOFILE_H_INCLUDED_
#define GEOFILE_H_INCLUDED_

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace geo {

/* A single geographic point. */
struct GeoPoint {
  QString name;          /* short name / identifier as stored in the file */
  QString description;   /* human readable description, may be empty       */
  double  latitude  = 0.0;  /* degrees, WGS84 */
  double  longitude = 0.0;  /* degrees, WGS84 */
  double  altitude  = 0.0;  /* meters; only meaningful if hasAltitude       */
  bool    hasAltitude = false;
  /*
   * Creation time of the point (GPX <time>, GDB timestamp), UTC.
   * Invalid (default) when the file carries no time; such points are also
   * written without a time.
   */
  QDateTime time;
};

/*
 * A route: an ordered sequence of references into GeoData::points.
 * The same point index may appear more than once (e.g. a there-and-back
 * route that revisits a waypoint).
 */
struct GeoRoute {
  QString       name;
  QString       description;
  QVector<int>  points;  /* indices into GeoData::points, in route order */
};

/* The complete result of parsing one file. */
struct GeoData {
  QVector<GeoPoint> points;
  QVector<GeoRoute> routes;

  bool isEmpty() const { return points.isEmpty() && routes.isEmpty(); }
};

/* Options for GeoFileParser::save(). */
struct SaveOptions {
  /*
   * GDB version to write: 3 (default; strings are UTF-8) or 2 (legacy
   * MapSource; strings are written in the Windows-1251 codepage so Cyrillic
   * survives -- symmetric to how parse() reads v1/v2 files).  Ignored for
   * non-GDB outputs.
   */
  int gdbVersion = 3;
};

/*
 * GeoFileParser converts a geo file (Garmin .gdb, GPX, GeoJSON) into the
 * plain GeoData model above, and can save a GeoData back to a file.
 *
 * Threading: GPSBabel relies on process-global state, so parsing is not
 * re-entrant.  This class serializes calls internally with a mutex, so it is
 * safe to call parse()/save() from several threads, but calls will not run in
 * parallel.
 */
class GeoFileParser {
public:
  GeoFileParser() = default;

  /*
   * Parse the file at filePath.  The format is chosen from the file
   * extension (see supportedExtensions()).  On success returns true and
   * fills out.  On failure returns false and, if errorMessage is non-null,
   * stores a human readable reason.
   */
  bool parse(const QString& filePath, GeoData& out, QString* errorMessage = nullptr);

  /*
   * Save data to the file at filePath.  The format is chosen from the file
   * extension: .gdb (Garmin MapSource, written as version 3 = UTF-8),
   * .gpx (GPX 1.0), .geojson / .json (GeoJSON FeatureCollection).
   * Waypoints and routes are written; an existing file is overwritten.
   * On failure returns false and, if errorMessage is non-null, stores a
   * human readable reason.
   */
  bool save(const QString& filePath, const GeoData& data, QString* errorMessage = nullptr);

  /* Same, with explicit options (e.g. SaveOptions{2} for a GDB v2 file). */
  bool save(const QString& filePath, const GeoData& data,
            const SaveOptions& options, QString* errorMessage = nullptr);

  /* File extensions (without the dot, lower case) this parser understands. */
  static QStringList supportedExtensions();

  /* File extensions save() can write (without the dot, lower case). */
  static QStringList supportedSaveExtensions();

  /* Convenience: true if filePath has a supported extension. */
  static bool isSupported(const QString& filePath);
};

} // namespace geo

#endif // GEOFILE_H_INCLUDED_
