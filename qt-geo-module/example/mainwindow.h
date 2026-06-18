/*
    Drag-and-drop demo for the geo::GeoFileParser module.

    Drop a Garmin .gdb file onto the window: its waypoints are listed on the
    left and its routes (as ordered references into those waypoints) on the
    right.
 */
#ifndef MAINWINDOW_H_INCLUDED_
#define MAINWINDOW_H_INCLUDED_

#include <QMainWindow>
#include <QString>

#include "geofile.h"

class QTableWidget;
class QTreeWidget;
class QLabel;
class QDragEnterEvent;
class QDropEvent;

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget* parent = nullptr);

  // Parse a file and show its contents.  Reports errors in the status bar.
  void loadFile(const QString& path);

protected:
  // Drag-and-drop entry points.
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

private slots:
  void openFileDialog();

private:
  void showData(const geo::GeoData& data, const QString& sourcePath);

  QTableWidget* pointsTable_;
  QTreeWidget*  routesTree_;
  QLabel*       hintLabel_;

  geo::GeoFileParser parser_;
};

#endif // MAINWINDOW_H_INCLUDED_
