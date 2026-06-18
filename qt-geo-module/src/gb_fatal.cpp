/*
    gb_fatal.cpp -- GUI-safe replacements for GPSBabel's fatal()/warning().

    GPSBabel's stock fatal() calls exit(1), which would terminate the host
    application when a malformed file is parsed.  For use as a library inside a
    GUI we instead throw a std::runtime_error so the caller can recover.  This
    file REPLACES the upstream fatal.cc (which must be excluded from the build).

    Built on top of GPSBabel, Copyright (C) Robert Lipe and contributors.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 */

#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>

#include <QDebug>
#include <QString>
#include <QtGlobal>

#include "defs.h"
#include "src/core/logging.h"

namespace {

// Used only while flushing a FatalMsg, to grab its accumulated text.
thread_local QString* g_capture = nullptr;

void captureHandler(QtMsgType /*type*/, const QMessageLogContext& /*ctx*/, const QString& msg)
{
  if (g_capture) {
    *g_capture = msg;
  }
}

} // namespace

[[noreturn]] void fatal(QDebug& msginstance)
{
  // The message text lives inside the QDebug buffer and is only emitted when
  // the QDebug is destroyed.  Temporarily install a handler so we can capture
  // it, then restore the host application's handler.
  QString captured;
  g_capture = &captured;
  QtMessageHandler prev = qInstallMessageHandler(captureHandler);
  {
    auto* myinstance = new FatalMsg;
    myinstance->swap(msginstance);
    delete myinstance; // flushes to captureHandler
  }
  qInstallMessageHandler(prev);
  g_capture = nullptr;

  throw std::runtime_error(captured.isEmpty()
                             ? std::string("gpsbabel: fatal error")
                             : captured.toStdString());
}

[[noreturn]] void fatal(const char* fmt, ...)
{
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  throw std::runtime_error(std::string(buf));
}

void warning(const char* fmt, ...)
{
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  fputs(buf, stderr);
}
