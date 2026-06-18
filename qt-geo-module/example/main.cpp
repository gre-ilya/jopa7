#include <QApplication>

#include "mainwindow.h"

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("GeoFileViewer"));

  MainWindow w;
  w.show();

  // Allow passing a file on the command line too.
  if (argc > 1) {
    w.loadFile(QString::fromLocal8Bit(argv[1]));
  }

  return app.exec();
}
