/*
    geodocument.cpp -- implementation of the lock-open-edit-save document.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include "geodocument.h"

#include <QLockFile>

namespace geo {

namespace {

QString lockPathFor(const QString& filePath)
{
  return filePath + QLatin1String(".lock");
}

void setError(QString* errorMessage, const QString& msg)
{
  if (errorMessage) {
    *errorMessage = msg;
  }
}

}  // namespace

GeoDocument::GeoDocument() = default;

GeoDocument::~GeoDocument() = default;  // unique_ptr<QLockFile> unlocks

std::unique_ptr<QLockFile>
GeoDocument::tryAcquireLock(const QString& filePath,
                            QString* errorMessage) const
{
  auto lock = std::make_unique<QLockFile>(lockPathFor(filePath));
  // A document stays open for as long as the user edits, so never treat the
  // lock as stale by age.  A lock whose owning process died is still detected
  // by PID and cleaned up automatically by tryLock().
  lock->setStaleLockTime(0);
  if (lock->tryLock(0)) {
    return lock;
  }
  if (lock->error() == QLockFile::LockFailedError) {
    qint64 pid = 0;
    QString host;
    QString app;
    if (lock->getLockInfo(&pid, &host, &app)) {
      setError(errorMessage,
               QStringLiteral("File is already opened by %1 (PID %2%3)")
                   .arg(app.isEmpty() ? QStringLiteral("another process") : app)
                   .arg(pid)
                   .arg(host.isEmpty() ? QString()
                                       : QStringLiteral(" on %1").arg(host)));
    } else {
      setError(errorMessage,
               QStringLiteral("File is already opened (lock file %1 exists)")
                   .arg(lockPathFor(filePath)));
    }
  } else {
    // PermissionError / UnknownError: e.g. a read-only directory.
    setError(errorMessage,
             QStringLiteral("Cannot create lock file %1")
                 .arg(lockPathFor(filePath)));
  }
  return nullptr;
}

bool GeoDocument::open(const QString& filePath, QString* errorMessage)
{
  close();

  std::unique_ptr<QLockFile> lock = tryAcquireLock(filePath, errorMessage);
  if (!lock) {
    return false;
  }

  GeoData parsed;
  if (!GeoFileParser().parse(filePath, parsed, errorMessage)) {
    return false;  // lock released with `lock`
  }

  m_lock = std::move(lock);
  m_path = filePath;
  m_data = std::move(parsed);
  return true;
}

bool GeoDocument::save(QString* errorMessage)
{
  if (!isOpen()) {
    setError(errorMessage, QStringLiteral("No file is open"));
    return false;
  }
  return GeoFileParser().save(m_path, m_data, errorMessage);
}

bool GeoDocument::saveAs(const QString& filePath, QString* errorMessage)
{
  if (!isOpen()) {
    setError(errorMessage, QStringLiteral("No file is open"));
    return false;
  }
  if (filePath == m_path) {
    return save(errorMessage);
  }

  std::unique_ptr<QLockFile> newLock = tryAcquireLock(filePath, errorMessage);
  if (!newLock) {
    return false;  // current document stays open and locked
  }
  if (!GeoFileParser().save(filePath, m_data, errorMessage)) {
    return false;  // newLock released; current document unchanged
  }

  m_lock = std::move(newLock);  // old lock released by the assignment
  m_path = filePath;
  return true;
}

void GeoDocument::close()
{
  m_lock.reset();
  m_path.clear();
  m_data = GeoData();
}

}  // namespace geo
