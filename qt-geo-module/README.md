# qt-geo-module — парсер гео-форматов на базе GPSBabel для qmake-проектов

Небольшой модуль, который встраивается в ваш Qt/qmake-проект и читает
гео-файлы (**Garmin `.gdb`**, **GPX**, **GeoJSON**) и сохраняет их обратно,
отдавая данные в простой модели, удобной для drag-and-drop в GUI.

Модель данных (`src/geofile.h`) намеренно **не зависит** от внутренностей
GPSBabel:

```cpp
namespace geo {

struct GeoPoint {        // географическая точка
    QString name;
    QString description;
    double  latitude, longitude, altitude;
    bool    hasAltitude;
    bool    standalone;  // false = точка живёт только внутри маршрута/трека
    // + непрозрачный payload полной информации (см. «Полнота при перезаписи»)
};

struct GeoRoute {        // маршрут = упорядоченные ссылки на точки
    QString      name;
    QString      description;
    QVector<int> points; // индексы в GeoData::points; индекс может повторяться
    bool         isTrack; // true = это трек (trk), а не маршрут (rte)
    // + непрозрачный payload полной информации (см. «Полнота при перезаписи»)
};

struct ParseOptions {
    bool includeTracks = true;  // false = не читать треки вовсе (см. ниже)
};

struct SaveOptions {
    int gdbVersion = 3;  // версия GDB при сохранении: 3 (UTF-8) или 2 (CP1251)
};

struct GeoData {
    QVector<GeoPoint> points;   // общий пул уникальных точек
    QVector<GeoRoute> routes;   // маршруты ссылаются на points по индексу
};

class GeoFileParser {
public:
    bool parse(const QString& filePath, GeoData& out, QString* error = nullptr);
    bool parse(const QString& filePath, GeoData& out,
               const ParseOptions& options, QString* error = nullptr);
    bool save(const QString& filePath, const GeoData& data, QString* error = nullptr);
    bool save(const QString& filePath, const GeoData& data,
              const SaveOptions& options, QString* error = nullptr);
    static QStringList supportedExtensions();      // что умеет parse()
    static QStringList supportedSaveExtensions();  // что умеет save()
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
исходников GPSBabel для чтения и записи GDB, GPX и GeoJSON (21 файл: `gdb.cc`, `gpx.cc`, `geojson.cc`,
`waypt.cc`, `route.cc`, `gbfile.cc`, `garmin_*`, `jeeps/gpsmath.cc`,
`src/core/*` и т.д.) плюс `-lz`.

## Сохранение в файл

`save()` пишет `GeoData` в файл; формат выбирается по расширению. Полный
пример — собрать данные прямо в рантайме и сохранить:

```cpp
#include "geofile.h"

// 1. Собираем модель из своих runtime-данных.
geo::GeoData data;

geo::GeoPoint p1;
p1.name        = QStringLiteral("Точка Москва");
p1.description = QStringLiteral("Красная площадь");
p1.latitude    = 55.7558;
p1.longitude   = 37.6173;
p1.altitude    = 150.0;
p1.hasAltitude = true;

geo::GeoPoint p2;
p2.name      = QStringLiteral("Питер");
p2.latitude  = 59.9391;
p2.longitude = 30.3159;

data.points = {p1, p2};

geo::GeoRoute route;
route.name   = QStringLiteral("Маршрут №1");
route.points = {0, 1, 0};      // индексы в data.points; повтор допустим
data.routes  = {route};

// 2. Сохраняем; формат — по расширению файла.
geo::GeoFileParser io;
QString err;
if (!io.save("/path/out.gpx", data, &err)) {     // GPX 1.0
    qWarning() << "Ошибка сохранения:" << err;
}
io.save("/path/out.gdb", data, &err);      // Garmin GDB (по умолчанию версия 3 = UTF-8)
io.save("/path/out.geojson", data, &err);  // GeoJSON FeatureCollection (.json — синоним)

// GDB версии 2 (старый MapSource):
geo::SaveOptions v2;
v2.gdbVersion = 2;
io.save("/path/out_v2.gdb", data, v2, &err);
```

Пишутся точки и маршруты; существующий файл перезаписывается. Особенности:

- **GDB** по умолчанию пишется **версией 3** (строки UTF-8). Через
  `SaveOptions{2}` можно записать **версию 2**: строки при этом кодируются в
  **Windows-1251** (как делает русский MapSource), так что кириллица выживает и
  там — символы, которых нет в CP1251, заменяются на `?`. Координаты в GDB
  хранятся как 32-битные semicircles — квантование ~3 мм.
- Маршрут с `isTrack = true` записывается **треком** (`trk`), без него —
  маршрутом (`rte`); `parse()` выставляет флаг при чтении, так что
  прочитанный трек при перезаписи остаётся треком.
- **GeoJSON** не имеет понятия «маршрут», поэтому маршруты записываются как
  `LineString` (в терминах GPSBabel — треки). Вершины `LineString` безымянные,
  так что при обратном чтении они не склеиваются с одноимёнными точками.
- При чтении (`parse`) **треки** из любых форматов теперь тоже возвращаются в
  `GeoData::routes`, наравне с маршрутами.

## Полнота при перезаписи: fidelity-payload

Цикл «прочитать файл → отредактировать часть полей → сохранить» **не теряет**
информацию, которой нет в простой модели. `parse()` прикрепляет к каждой точке
и каждому маршруту/треку **непрозрачный payload** — полную копию того, что
прочитал GPSBabel (иконки, категории, классы точек, времена создания, времена и
скорости точек трека, разбиение на `trkseg`, геометрию автопрокладки маршрутов
GDB, GPX-расширения `gpxx:*`, цвет/стиль линии, URL...). `save()` берёт payload
за основу и накладывает поверх только редактируемые поля (`name`,
`description`, `lat/lon`, `altitude`).

Ваш код при этом не меняется: работаете с теми же простыми полями, payload
переносится автоматически. Точка, созданная в рантайме (без payload), пишется
«с нуля», как раньше.

Свойства и границы:

- **Точки, живущие только в маршруте/треке** (например, безымянные точки
  трека), помечаются `standalone = false` и при сохранении **не**
  дублируются в список путевых точек файла. **В GUI показывайте только
  `p.standalone == true`** — иначе список точек «зальёт» точками треков:

  ```cpp
  for (const geo::GeoPoint& p : data.points) {
      if (!p.standalone) continue;   // точки треков пропускаем
      addRowToTable(p);
  }
  ```
- Если треки не нужны вовсе — `ParseOptions po; po.includeTracks = false;
  parser.parse(path, data, po, &err);` — точки из треков тогда не создаются.
  ⚠️ Но данные, прочитанные без треков, при `save()` поверх исходного файла
  **потеряют его треки** — для сценария «открыл-правил-сохранил» используйте
  фильтр по `standalone`, а не эту опцию.
- Проверено тестами: пересохранение реального файла MapSource **байт-в-байт
  идемпотентно** (A → B → B == A), в GPX выживают `sym`, `<time>` точки,
  времена точек трека и второй `<trkseg>`; правка имени при этом применяется.
- Полная полнота — в паре «формат → тот же формат» (gdb→gdb, gpx→gpx). При
  конвертации (gdb→gpx) сохранится то, что умеет целевой формат; GeoJSON
  беднее всех — там payload почти не используется.
- Если **изменён состав/порядок точек** маршрута или трека, по-точечный
  payload этой линии (геометрия автопрокладки, времена) применить нельзя —
  такие точки пишутся из их собственных payload'ов/«с нуля» (MapSource
  пересчитает автопрокладку сам).
- Результат семантически полный, но не обязан быть побайтово равен
  **исходному** файлу MapSource (порядок незначащих полей может отличаться);
  повторные пересохранения нашим модулем уже побайтово стабильны.

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
- Все подключаемые файлы GPSBabel — это переносимый C++/Qt без прямых
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

Поддерживаются GDB и GPX. Чтобы добавить ещё один формат GPSBabel
(например KML):

1. добавьте его `.cc`-файлы (и новые зависимости) в `geobabel.pri`;
2. добавьте ветку в `makeReader()` в `geofile.cpp`;
3. добавьте расширение в `GeoFileParser::supportedExtensions()`.

Архитектура для этого уже готова: ридер — это полиморфный `Format*`, а вся
остальная логика (инициализация, обход глобальных списков, сборка `GeoData`)
от формата не зависит.

## Решение проблем

### Конфликт имён объектных файлов (`route.cpp` ↔ `route.cc`) — частая причина `undefined reference`

Если в линковке падают символы из конкретного файла ядра (например,
`global_route_list` / `route_add_wpt` из `route.cc`, видно как ошибка в
`geofile.cpp` на строке с `*global_route_list`), а другие файлы при этом
собрались — почти наверняка **конфликт имён `.o`**.

qmake именует объектные файлы по *базовому* имени исходника без учёта
расширения и каталога. Если в вашем проекте есть `route.cpp`, а у GPSBabel —
`route.cc`, оба компилируются в **`route.o`** в одном build-каталоге, и один
молча перезатирает другой → символы из `route.cc` пропадают. То же случится с
`util.cpp`/`util.cc`, `session.*` и любыми другими совпадениями.

**Решение (рекомендуется): собрать модуль отдельной статической библиотекой**
`qt-geo-module/lib/geobabel_lib.pro`. Тогда объекты GPSBabel живут в своём
build-каталоге и ни с чем не сталкиваются:
```sh
mkdir build-geobabel && cd build-geobabel
qmake /path/to/qt-geo-module/lib/geobabel_lib.pro
make            # -> libgeobabel.a   (geobabel.lib на MSVC)
```
В вашем `.pro` — линкуем готовую библиотеку (а НЕ include основного `.pri`):
```pro
QT += core
CONFIG += c++17
GEOBABEL_BUILD_DIR = /path/to/build-geobabel
include(/path/to/qt-geo-module/lib/link_geobabel.pri)
# теперь ваши route.cpp / util.cpp не конфликтуют с ядром GPSBabel
```

**Быстрая альтернатива (одной строкой):** добавьте в свой `.pro`
`CONFIG += object_parallel_to_source` — `.o` будут класться рядом с исходниками,
и базовые имена перестанут сталкиваться. Минус: `.o` пишутся прямо в дерево
исходников (в т.ч. в `GPSBABEL_SRC`).

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
