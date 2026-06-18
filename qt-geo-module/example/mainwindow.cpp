#include "mainwindow.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMimeData>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// Pull the supported local file paths out of a drag/drop mime payload.
QStringList supportedPaths(const QMimeData* mime)
{
  QStringList paths;
  if (mime && mime->hasUrls()) {
    const QList<QUrl> urls = mime->urls();
    for (const QUrl& url : urls) {
      if (!url.isLocalFile()) {
        continue;
      }
      const QString path = url.toLocalFile();
      if (geo::GeoFileParser::isSupported(path)) {
        paths << path;
      }
    }
  }
  return paths;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
  : QMainWindow(parent)
{
  setWindowTitle(QStringLiteral("Geo file viewer (GPSBabel / GDB)"));
  resize(900, 560);
  setAcceptDrops(true); // enable drag-and-drop onto the whole window

  // ---- Points table (left) ----
  pointsTable_ = new QTableWidget(this);
  pointsTable_->setColumnCount(6);
  pointsTable_->setHorizontalHeaderLabels(
      {QStringLiteral("#"), QStringLiteral("Name"), QStringLiteral("Lat"),
       QStringLiteral("Lon"), QStringLiteral("Alt (m)"),
       QStringLiteral("Description")});
  pointsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  pointsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  pointsTable_->verticalHeader()->setVisible(false);
  pointsTable_->horizontalHeader()->setStretchLastSection(true);

  // ---- Routes tree (right) ----
  routesTree_ = new QTreeWidget(this);
  routesTree_->setColumnCount(3);
  routesTree_->setHeaderLabels(
      {QStringLiteral("Route / point"), QStringLiteral("Point #"),
       QStringLiteral("Coordinates")});

  auto* splitter = new QSplitter(Qt::Horizontal, this);
  splitter->addWidget(pointsTable_);
  splitter->addWidget(routesTree_);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);

  // ---- "Drop a file here" hint shown until something is loaded ----
  hintLabel_ = new QLabel(
      QStringLiteral("Drag a Garmin .gdb file here\n(or use File → Open…)"),
      this);
  hintLabel_->setAlignment(Qt::AlignCenter);
  QFont f = hintLabel_->font();
  f.setPointSize(f.pointSize() + 4);
  hintLabel_->setFont(f);
  hintLabel_->setStyleSheet(QStringLiteral("color:#888;"));

  auto* stack = new QStackedWidget(this);
  stack->addWidget(hintLabel_); // index 0
  stack->addWidget(splitter);   // index 1
  setCentralWidget(stack);

  // ---- Menu ----
  QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
  QAction* openAct = fileMenu->addAction(QStringLiteral("&Open…"));
  openAct->setShortcut(QKeySequence::Open);
  connect(openAct, &QAction::triggered, this, &MainWindow::openFileDialog);
  fileMenu->addSeparator();
  QAction* quitAct = fileMenu->addAction(QStringLiteral("&Quit"));
  quitAct->setShortcut(QKeySequence::Quit);
  connect(quitAct, &QAction::triggered, this, &QWidget::close);

  statusBar()->showMessage(QStringLiteral("Ready. Drop a .gdb file to begin."));
}

void MainWindow::openFileDialog()
{
  const QString filter =
      QStringLiteral("Geo files (*.gdb);;All files (*)");
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Open geo file"), QString(), filter);
  if (!path.isEmpty()) {
    loadFile(path);
  }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
  if (!supportedPaths(event->mimeData()).isEmpty()) {
    event->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent* event)
{
  const QStringList paths = supportedPaths(event->mimeData());
  if (paths.isEmpty()) {
    return;
  }
  event->acceptProposedAction();
  // Load the first supported file that was dropped.
  loadFile(paths.first());
}

void MainWindow::loadFile(const QString& path)
{
  geo::GeoData data;
  QString error;
  if (!parser_.parse(path, data, &error)) {
    statusBar()->showMessage(
        QStringLiteral("Failed to parse %1: %2")
            .arg(QFileInfo(path).fileName(), error));
    return;
  }
  showData(data, path);
}

void MainWindow::showData(const geo::GeoData& data, const QString& sourcePath)
{
  // Reveal the data views (hide the hint).
  if (auto* stack = qobject_cast<QStackedWidget*>(centralWidget())) {
    stack->setCurrentIndex(1);
  }

  // ---- Points ----
  pointsTable_->setRowCount(data.points.size());
  for (int i = 0; i < data.points.size(); ++i) {
    const geo::GeoPoint& p = data.points[i];
    pointsTable_->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
    pointsTable_->setItem(i, 1, new QTableWidgetItem(p.name));
    pointsTable_->setItem(i, 2,
        new QTableWidgetItem(QString::number(p.latitude, 'f', 6)));
    pointsTable_->setItem(i, 3,
        new QTableWidgetItem(QString::number(p.longitude, 'f', 6)));
    pointsTable_->setItem(i, 4,
        new QTableWidgetItem(p.hasAltitude
                                 ? QString::number(p.altitude, 'f', 1)
                                 : QString()));
    pointsTable_->setItem(i, 5, new QTableWidgetItem(p.description));
  }
  pointsTable_->resizeColumnsToContents();
  pointsTable_->horizontalHeader()->setStretchLastSection(true);

  // ---- Routes ----
  routesTree_->clear();
  for (const geo::GeoRoute& route : data.routes) {
    QString title = route.name.isEmpty() ? QStringLiteral("(unnamed route)")
                                         : route.name;
    auto* top = new QTreeWidgetItem(routesTree_);
    top->setText(0, QStringLiteral("%1  [%2 pts]")
                        .arg(title)
                        .arg(route.points.size()));
    if (!route.description.isEmpty()) {
      top->setToolTip(0, route.description);
    }
    // Each route point references a shared point by index; the same index may
    // legitimately appear more than once.
    for (int idx : route.points) {
      auto* child = new QTreeWidgetItem(top);
      if (idx >= 0 && idx < data.points.size()) {
        const geo::GeoPoint& p = data.points[idx];
        child->setText(0, p.name);
        child->setText(1, QString::number(idx));
        child->setText(2, QStringLiteral("%1, %2")
                              .arg(p.latitude, 0, 'f', 6)
                              .arg(p.longitude, 0, 'f', 6));
      } else {
        child->setText(0, QStringLiteral("<invalid index>"));
        child->setText(1, QString::number(idx));
      }
    }
    top->setExpanded(true);
  }
  routesTree_->resizeColumnToContents(0);
  routesTree_->resizeColumnToContents(1);

  statusBar()->showMessage(
      QStringLiteral("%1: %2 points, %3 routes")
          .arg(QFileInfo(sourcePath).fileName())
          .arg(data.points.size())
          .arg(data.routes.size()));
}
