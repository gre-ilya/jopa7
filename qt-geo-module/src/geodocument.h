/*
    geodocument.h -- a "document" wrapper: open-with-lock / edit / save.

    Gives a Qt application the MapSource-like workflow:

        geo::GeoDocument doc;
        if (!doc.open(path, &err))   // takes the lock, then parses the file
            ...                      // e.g. "already opened by PID 1234"
        doc.data().points[0].name = "...";   // edit in memory
        doc.save(&err);              // write back (the lock is still ours)
        doc.close();                 // release the lock (also in ~GeoDocument)

    Locking uses QLockFile: a "<file>.lock" marker next to the document with
    the owner PID/host inside.  It protects against a second copy of *this*
    application (or a second window of it) opening the same file on any OS.
    It is advisory: unrelated programs that do not check the marker are not
    blocked.  If the owning process crashes, the stale lock is detected by PID
    and removed automatically on the next open().

    This header stays free of GPSBabel internals, like geofile.h.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */
#ifndef GEODOCUMENT_H_INCLUDED_
#define GEODOCUMENT_H_INCLUDED_

#include <memory>

#include <QString>

#include "geofile.h"

class QLockFile;

namespace geo {

class GeoDocument {
public:
  GeoDocument();
  ~GeoDocument();  // releases the lock

  GeoDocument(const GeoDocument&) = delete;
  GeoDocument& operator=(const GeoDocument&) = delete;

  /*
   * Lock and parse filePath.  If another GeoDocument (in any process) holds
   * the lock, returns false with a message naming the owner; the file is not
   * read.  A previously opened document is closed first.
   */
  bool open(const QString& filePath, QString* errorMessage = nullptr);

  /* Write data() back to the opened file.  The lock is already ours. */
  bool save(QString* errorMessage = nullptr);

  /*
   * Write data() to a different file and switch this document (and its lock)
   * to it.  Fails if the target is locked by someone else; on failure the
   * current file stays open and locked.
   */
  bool saveAs(const QString& filePath, QString* errorMessage = nullptr);

  /* Release the lock and clear the document. Safe to call when not open. */
  void close();

  bool isOpen() const { return m_lock != nullptr; }
  QString filePath() const { return m_path; }

  GeoData& data() { return m_data; }
  const GeoData& data() const { return m_data; }

private:
  std::unique_ptr<QLockFile> tryAcquireLock(const QString& filePath,
                                            QString* errorMessage) const;

  std::unique_ptr<QLockFile> m_lock;
  QString m_path;
  GeoData m_data;
};

}  // namespace geo

#endif  // GEODOCUMENT_H_INCLUDED_
