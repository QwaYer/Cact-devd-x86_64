# План работ: Cact-devd x86_64 (CactOS 3.0.0)

Форк от `2.0.0` (i686). `MAJOR` → 3 по правилу «`MAJOR` = смена архитектурного
поколения»: демон перелинковывается под ABI ядра 3.0.0 и libc 3.0.0
(`CactKernel-x86_64`, `CactLibc-x86_64`, решения 5/9 ядра, решение 5 libc).

Этот файл — план, а не обещание. Инвентарь снят по коду; при расхождении верить
коду.

## Что уже хорошо

Cact-devd — один `main.c` без ассемблера и без прямых syscall: всё идёт через
`clibc.so`. Архитектурно-зависимы только сборка и скрипт линковки.

## Инвентарь 32-битных поверхностей

- `cross/i686-cact-clang.ini` — `-m32`, `cpu=i686`.
- `meson.build` — `ld -m elf_i386 -pie`; комментарий ссылается на i686-файл и
  `CactLibc-x86_32`.
- `link.ld` — `.rel.dyn`/`.rel.plt` (в x86_64 — `.rela.*`); нет `.data.rel.ro`.
  База `0x08000000` не двигается (решения 5/9 ядра).
- `meson_options.txt` — дефолты `../CactLibc-x86_32`,
  `../LocalRepoCactOS-x86_32/...`.

## Решения

1. **Тулчейн как у cactsole/cgoct**: `cross/x86_64-cact-clang.ini` (`-m64`),
   `ld -m elf_x86_64`, дефолты `meson_options.txt` → `../*-x86_64`.
2. **База `0x08000000` не двигается** (решение 5 libc).
3. **Скрипт линковки под ELF64**: `.rela.dyn`/`.rela.plt` и `.data.rel.ro`,
   пара `.rel.*` остаётся против orphan-секций.

## Этапы

Порт — один этап: меняются только тулчейн и скрипт линковки; коммит один.

### Фаза 0 — тулчейн и скрипт линковки

**Milestone:** `ninja -C build-meson` собирает `devd`; `readelf -h` —
`ELF64`/`EM_X86_64`.

## Проверки

- **Кросс-сборка** — `meson setup build-meson --cross-file
  cross/x86_64-cact-clang.ini -Dcactlib=../CactLibc-x86_64 && ninja -C
  build-meson` (нужен собранный `CactLibc-x86_64/build-meson/start.o` и
  `clibc.so`).
- **Заголовок/релокации** — `readelf -h`/`readelf -r build-meson/devd`.
- **На железе/в QEMU** — за ядром, демон стартует как `/usr/sbin/devd`.

## Открытые вопросы

- **Своего `VERSION`/README нет** — версия в репозитории не заведена.
- **Образ — `ET_EXEC`** (как у cactsole/cgoct 2.0.0): ядро мапит по `PT_LOAD`
  без проверки `e_type`.
