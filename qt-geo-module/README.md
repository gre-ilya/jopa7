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

> **Важно.** `geobabel.pri` сам добавляет в сборку *полный* набор нужных
> исходников GPSBabel (`gdb.cc`, `route.cc`, `waypt.cc`, …). **Не добавляйте их
> в свой `SOURCES` вручную** — подключения `.pri` достаточно. Путь
> `GPSBABEL_SRC` должен указывать на корень дерева исходников GPSBabel (где
> лежат `gdb.cc`, `defs.h`, `jeeps/`, `src/core/`). Если он не задан, берётся
> родительский каталог модуля.

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

## Кросс-платформенность (Windows + Linux)

Модуль рассчитан на сборку и под **Windows**, и под **Linux** (и macOS) одним и
тем же `.pri`:

- **zlib не как системная зависимость.** `gbfile.cc` использует zlib для чтения
  gzip-сжатых входов. По умолчанию `.pri` компилирует **встроенный в GPSBabel
  zlib** (`$$GPSBABEL_SRC/zlib`, режим «included» — родной дефолт GPSBabel), так
  что внешний `-lz`/`zlib.lib` не нужен и сборка одинакова на всех платформах.
  Переопределяется переменной перед `include(...)`:
  - `GEOBABEL_ZLIB = bundled` — встроенный zlib (по умолчанию);
  - `GEOBABEL_ZLIB = system` — системный zlib (`-lz` / `zlib.lib` + `HAVE_LIBZ`);
  - `GEOBABEL_ZLIB = none` — без zlib (`ZLIB_INHIBITED`); для чистого `.gdb`
    достаточно, т.к. GDB никогда не бывает gzip-сжат.
- **Платформенные дефайны** выставляются автоматически: `__WIN32__` на Windows,
  `_CRT_SECURE_NO_WARNINGS` / `_CRT_NONSTDC_NO_WARNINGS` на MSVC. `M_PI` GPSBabel
  определяет сам в `defs.h`, поэтому `_USE_MATH_DEFINES` не требуется.
- Все 17 подключаемых файлов GPSBabel — это переносимый C++/Qt без прямых
  POSIX/WinAPI-заголовков; серийные порты (`gbser`) для чтения GDB не нужны и не
  подключаются.

Требования: **Qt 5.12+** (или Qt 6) и компилятор с C++17 — на Windows это
**MSVC 2017+**, **clang** (`clang-cl` / `win32-clang-msvc` или clang+MinGW /
`win32-clang-g++`) или MinGW, как и у самого GPSBabel. CRT-дефайны
выставляются для всех Windows-тулчейнов, поэтому отдельной настройки под clang
не требуется.

> Замечание о тестировании: сборка и runtime проверены мной на Linux в обоих
> режимах zlib **двумя компиляторами — g++ 13 и clang 18** (`qmake -spec
> linux-clang`), оба собирают весь модуль без ошибок. Это покрывает совместимость
> с фронтендом clang; на самой Windows я сборку в этой среде не запускал, но
> модуль использует ровно тот же набор исходников и дефайнов, который GPSBabel
> штатно собирает под Windows (включая clang-tidy/`clang-diagnostic-*` в его CI).

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

## Решение проблем

### `undefined reference to route_add_wpt(...)` (и другие символы GPSBabel) при линковке

Симптом: `gdb.cc` компилируется, но на этапе линковки куча `undefined
reference` на функции ядра GPSBabel (`route_add_wpt`, `waypt_add`,
`route_add_head`, …). Это значит, что в линковку попал только `gdb.cc`, а
остальные `.cc`-файлы ядра — нет. Две причины:

1. **`include(...geobabel.pri)` не сработал из-за неверного пути.** В qmake
   неудачный `include()` **не фатален** — он печатает
   `Cannot read .../geobabel.pri: No such file or directory` и продолжает, после
   чего `.pri` не добавляет ничего. Проверьте вывод `qmake` на эту строку и
   укажите корректный путь к `geobabel.pri`.
2. **`GPSBABEL_SRC` указывает не туда** (или вы скопировали только часть
   исходников / добавили `gdb.cc` в свой `SOURCES` руками). Начиная с этой
   версии `geobabel.pri` ловит это сам и падает на этапе `qmake` с понятным
   сообщением:
   ```
   Project ERROR: geobabel.pri: GPSBabel sources not found under GPSBABEL_SRC='...'.
   ```
   Задайте `GPSBABEL_SRC` на корень дерева GPSBabel **до** `include(...)` и
   **не** добавляйте `.cc`-файлы GPSBabel в свой `SOURCES` вручную — `.pri`
   подключает полный набор сам.

Минимальный рабочий `.pro`:
```pro
QT += core
CONFIG += c++17 console
GPSBABEL_SRC = /абсолютный/путь/к/gpsbabel      # каталог с gdb.cc, defs.h, jeeps/, src/core/
include(/абсолютный/путь/к/qt-geo-module/geobabel.pri)
SOURCES += main.cpp
```

## Лицензия

Модуль построен на GPSBabel и распространяется на тех же условиях — **GPL v2 или
новее** (см. `../COPYING`).
