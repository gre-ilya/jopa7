# qt-geo-module — парсер гео-форматов на базе GPSBabel для qmake-проектов

Небольшой модуль, который встраивается в ваш Qt/qmake-проект и читает
гео-файлы (в первую очередь **Garmin `.gdb`**), отдавая их в простой модели
данных, удобной для drag-and-drop в GUI.

Модель данных (`src/geofile.h`) намеренно **не зависит** от внутренностей
GPSBabel:

```cpp
namespace geo {

struct GeoPoint {        // географическая точка
    QString name;
    QString description;
    double  latitude, longitude, altitude;
    bool    hasAltitude;
};

struct GeoRoute {        // маршрут = упорядоченные ссылки на точки
    QString      name;
    QString      description;
    QVector<int> points; // индексы в GeoData::points; индекс может повторяться
};

struct GeoData {
    QVector<GeoPoint> points;   // общий пул уникальных точек
    QVector<GeoRoute> routes;   // маршруты ссылаются на points по индексу
};

class GeoFileParser {
public:
    bool parse(const QString& filePath, GeoData& out, QString* error = nullptr);
    static QStringList supportedExtensions();
    static bool isSupported(const QString& filePath);
};

} // namespace geo
```

Это в точности соответствует требованию: **маршрут хранит порядок и ссылки
(индексы) на точки из общего пула, одна точка может встречаться несколько раз**
(тогда её индекс повторяется в `GeoRoute::points`).

## Состав

```
qt-geo-module/
├── geobabel.pri          # qmake-include: подключает модуль + нужный кусок GPSBabel
├── gbversion.h           # статическая замена генерируемого CMake-ом заголовка
├── src/
│   ├── geofile.h         # публичный API + модель данных (без зависимостей от GPSBabel)
│   ├── geofile.cpp       # обёртка вокруг GPSBabel-ридера GDB
│   └── gb_fatal.cpp      # GUI-safe замена fatal()/warning(): бросает исключение, не exit()
└── example/              # GUI-пример с drag-and-drop
    ├── example.pro
    ├── main.cpp
    ├── mainwindow.h
    └── mainwindow.cpp
```

## Подключение к своему qmake-проекту

В вашем `.pro`:

```pro
QT += core            # для GUI ещё: gui widgets
CONFIG += c++17

# Где лежит дерево исходников GPSBabel. По умолчанию — родитель модуля
# (верно, если модуль лежит внутри дерева GPSBabel, как в этом репозитории).
# GPSBABEL_SRC = /path/to/gpsbabel-source

include(/path/to/qt-geo-module/geobabel.pri)
```

И в коде:

```cpp
#include "geofile.h"

geo::GeoFileParser parser;
geo::GeoData data;
QString error;
if (parser.parse("/path/to/track.gdb", data, &error)) {
    // data.points, data.routes
} else {
    qWarning() << "Ошибка разбора:" << error;
}
```

`geobabel.pri` добавляет в сборку обёртку и **минимально необходимый набор**
исходников GPSBabel для чтения GDB (17 файлов: `gdb.cc`, `waypt.cc`, `route.cc`,
`gbfile.cc`, `garmin_*`, `jeeps/gpsmath.cc`, `src/core/*` и т.д.) плюс `-lz`.

## GUI-пример (drag-and-drop)

```bash
cd qt-geo-module/example
qmake && make
./geofileviewer                 # перетащите .gdb в окно
./geofileviewer file.gdb        # или передайте файл аргументом
```

Окно принимает drag-and-drop `.gdb`-файлов (`setAcceptDrops` +
`dragEnterEvent`/`dropEvent` в `mainwindow.cpp`), показывает точки в таблице
слева и маршруты деревом справа, где каждый пункт маршрута — это ссылка
(индекс) на точку из общего пула.

## Важные детали реализации

- **GUI-безопасность.** Штатный `fatal()` в GPSBabel вызывает `exit(1)`, что
  убило бы хост-приложение на битом файле. Модуль подключает свой `gb_fatal.cpp`
  (бросает `std::runtime_error`) **вместо** апстримного `fatal.cc`, поэтому
  `parse()` на некорректном файле просто возвращает `false` и текст ошибки.
- **Потокобезопасность.** GPSBabel использует глобальное состояние и не
  реентерабелен. `GeoFileParser::parse()` сериализует вызовы внутренним
  мьютексом и очищает глобальные списки до и после разбора.
- **Дедупликация точек.** Точки маршрута в GDB хранятся копиями; модуль
  сопоставляет их с самостоятельными путевыми точками по имени+координатам,
  так что повторное использование точки даёт повторяющийся индекс, а не дубль.

## Как добавить другие форматы

GDB — основной поддерживаемый формат. Чтобы добавить ещё один формат GPSBabel
(например GPX):

1. добавьте его `.cc`-файлы (и новые зависимости) в `geobabel.pri`;
2. добавьте ветку в `makeReader()` в `geofile.cpp`;
3. добавьте расширение в `GeoFileParser::supportedExtensions()`.

Архитектура для этого уже готова: ридер — это полиморфный `Format*`, а вся
остальная логика (инициализация, обход глобальных списков, сборка `GeoData`)
от формата не зависит.

## Лицензия

Модуль построен на GPSBabel и распространяется на тех же условиях — **GPL v2 или
новее** (см. `../COPYING`).
