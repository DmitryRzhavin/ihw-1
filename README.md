# ИДЗ №1 АКОС — вариант 32 «Кухня ресторана»

Консольное приложение на C11 для Linux, реализующее **последовательную дискретно-событийную имитацию** работы кухни ресторана.

> **Важно:** в ИДЗ №1 посетители, официанты, повара и координатор кухни являются самостоятельными сущностями модели, но не отдельными `pthread`-потоками. Работа ресторана моделируется последовательно одним процессом.

## Что реализовано

- посетители, официанты, повара и координатор кухни;
- заказы из одного или нескольких блюд;
- блюда как последовательности зависимых операций;
- оборудование четырёх типов:
  - `stove` — плита;
  - `oven` — печь;
  - `grill` — гриль;
  - `table` — рабочий стол;
- контроль занятости поваров и оборудования;
- кухонная очередь ограниченной вместимости;
- зона готовых блюд ограниченной вместимости;
- правила приоритета `FIFO` и `LIFO`;
- явные заказы из конфигурации;
- воспроизводимая генерация заказов по `seed`;
- постоянные или индивидуальные интервалы прихода посетителей;
- обнаружение и явная отмена невыполнимых заказов;
- подробный событийный вывод;
- сохранение журнала через файловые дескрипторы `open` / `write` / `close`;
- корректное завершение по `SIGINT` (`Ctrl+C`) и `SIGTERM`;
- регулируемая задержка отображения событий;
- итоговая статистика;
- тестовые конфигурации и автоматический Bash-скрипт.

---

## Структура проекта

```text
IHW_1/
├── CMakeLists.txt
├── README.md
├── REPORT.md
├── configs/
│   ├── demo.conf
│   ├── impossible.conf
│   ├── lifo.conf
│   ├── limited_queues.conf
│   ├── random_orders.conf
│   └── ready_full.conf
├── examples/
│   └── demo_output.txt
├── include/
│   ├── config.h
│   ├── logger.h
│   ├── model.h
│   └── simulation.h
├── scripts/
│   ├── build.sh
│   └── run_tests.sh
├── src/
│   ├── config.c
│   ├── logger.c
│   ├── main.c
│   └── simulation.c
└── tests/
    └── README.md
```

Каталог `build/` и файлы `*.log` являются генерируемыми и не должны добавляться в Git.

---

## Требования к окружению

Проверено в Linux Ubuntu.

Необходимы:

```bash
gcc
cmake
bash
```

Проверить наличие:

```bash
gcc --version
cmake --version
bash --version
```

---

## Сборка

Из корня проекта `IHW_1`:

```bash
chmod +x scripts/*.sh
./scripts/build.sh
```

После успешной сборки исполняемый файл находится здесь:

```text
build/restaurant
```

Альтернативно можно собрать вручную:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

---

## Основной запуск

Демонстрационный сценарий:

```bash
./build/restaurant --config configs/demo.conf
```

Для защиты удобнее включить небольшую реальную задержку между сообщениями:

```bash
./build/restaurant --config configs/demo.conf --delay-ms 100
```

Задержка влияет только на скорость отображения событий в терминале и **не изменяет модельное время**.

---

## Полезные сценарии

Ограниченные очереди:

```bash
./build/restaurant --config configs/limited_queues.conf --delay-ms 100
```

Невыполнимый заказ:

```bash
./build/restaurant --config configs/impossible.conf --delay-ms 100
```

Ограниченная зона готовых блюд:

```bash
./build/restaurant --config configs/ready_full.conf --delay-ms 100
```

Приоритет LIFO:

```bash
./build/restaurant --config configs/lifo.conf --delay-ms 100
```

Воспроизводимые случайно формируемые заказы:

```bash
./build/restaurant --config configs/random_orders.conf
```

---

## Тесты

Запуск всех проверок:

```bash
./scripts/run_tests.sh
```

Ожидаемый итог:

```text
All tests passed.
```

Также используются встроенные CTest-тесты:

```bash
ctest --test-dir build --output-on-failure
```

---

## Параметры командной строки

Получить справку:

```bash
./build/restaurant --help
```

Основные параметры:

```text
--config FILE
--log FILE
--no-log
--delay-ms N
--priority fifo|lifo

--visitors N
--waiters N
--cooks N
--arrival-interval N
--dishes-per-order N
--kitchen-cap N
--ready-cap N
--stoves N
--ovens N
--grills N
--tables N
--seed N
```

Если `--config` не указан, программа запрашивает параметры интерактивно.

---

## Логирование

По умолчанию события записываются в:

```text
restaurant.log
```

Указать другой файл:

```bash
./build/restaurant --config configs/demo.conf --log demo.log
```

Отключить лог:

```bash
./build/restaurant --config configs/demo.conf --no-log
```

Посмотреть лог:

```bash
less restaurant.log
```

---

## Завершение по Ctrl+C

Для проверки можно запустить сценарий с большим количеством автоматически формируемых заказов:

```bash
./build/restaurant \
  --config configs/random_orders.conf \
  --visitors 20 \
  --delay-ms 300
```

После появления нескольких событий нажать:

```text
Ctrl+C
```

Программа завершает основной цикл в безопасной точке, выводит статистику и возвращает код `130`.

Проверить код возврата:

```bash
echo $?
```

---

## Формат конфигурационного файла

Пример:

```text
visitors=4
waiters=2
cooks=3

arrival_interval=2
kitchen_queue_capacity=4
ready_queue_capacity=2

priority=fifo
seed=42

equipment_stove=1
equipment_oven=1
equipment_grill=1
equipment_table=2

dish=Soup|stove:3,table:1
dish=Roast|oven:4,table:1
dish=Grilled fish|grill:3,table:2

order=Soup,Roast
order=Roast,Grilled fish
order=Grilled fish,Soup
order=Soup,Roast
```

Описание блюда:

```text
dish=Название|оборудование:время,оборудование:время,...
```

Например:

```text
dish=Soup|stove:3,table:1
```

означает, что для приготовления блюда `Soup` сначала используется плита в течение 3 модельных единиц времени, затем рабочий стол в течение 1 единицы.

Если в конфигурации используются явные строки `order=`, необходимо задать **ровно один заказ на каждого посетителя**.

Если строки `order=` отсутствуют, программа автоматически формирует заказы из известных блюд, используя `dishes_per_order` и `seed`.

---

## Как работает модель

Программа использует модельное время.

Она не ждёт реальную длительность приготовления каждой операции. Вместо этого модель определяет ближайшее следующее событие и переводит модельное время непосредственно к этому моменту.

В общих чертах цикл выглядит так:

1. обработка завершившихся операций;
2. освобождение поваров и оборудования;
3. разблокировка следующих операций блюда;
4. обработка готовых блюд;
5. сборка готовых заказов;
6. приём ожидающих посетителей свободными официантами;
7. обработка новых приходов;
8. постановка доступных операций в кухонную очередь;
9. назначение операций свободным поварам;
10. переход к ближайшему следующему событию.

Такой подход позволяет последовательно моделировать параллельно происходящие в предметной области процессы без создания реальных потоков ОС.

---

## Основные инварианты

Программа поддерживает следующие ограничения:

- одна единица оборудования одновременно используется не более чем одной операцией;
- один повар одновременно выполняет не более одной операции;
- операция блюда не начинается до завершения предыдущей;
- блюдо считается готовым только после завершения всех операций;
- заказ выдаётся только после готовности всех входящих в него блюд;
- заказ выдаётся только своему посетителю.

---

## Проверенные результаты

Для `configs/demo.conf`:

```text
Termination: normal
Model time: 14
Visitors arrived: 4 / 4
Orders accepted: 4
Orders delivered: 4
Orders canceled as impossible: 0
Dishes created / ready: 8 / 8
Operations created / started / completed: 16 / 16 / 16
Average kitchen-queue wait: 0.31 model units
Cook utilization: 88.10%
```

Для `configs/limited_queues.conf`:

```text
Termination: normal
Model time: 11
Visitors arrived: 3 / 3
Orders accepted: 3
Orders delivered: 3
Orders canceled as impossible: 0
Dishes created / ready: 6 / 6
Operations created / started / completed: 12 / 12 / 12
Average kitchen-queue wait: 1.08 model units
Cook utilization: 90.91%
```

Для `configs/impossible.conf`:

```text
Termination: normal
Model time: 4
Visitors arrived: 2 / 2
Orders accepted: 2
Orders delivered: 1
Orders canceled as impossible: 1
Dishes created / ready: 1 / 1
Operations created / started / completed: 2 / 2 / 2
```
