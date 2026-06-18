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

/*
 * GeoFileParser converts a geo file (primarily Garmin .gdb) into the plain
 * GeoData model above.
 *
 * Threading: GPSBabel relies on process-global state, so parsing is not
 * re-entrant.  This class serializes calls internally with a mutex, so it is
 * safe to call parse() from several threads, but calls will not run in
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

  /* File extensions (without the dot, lower case) this parser understands. */
  static QStringList supportedExtensions();

  /* Convenience: true if filePath has a supported extension. */
  static bool isSupported(const QString& filePath);
};

} // namespace geo

#endif // GEOFILE_H_INCLUDED_
