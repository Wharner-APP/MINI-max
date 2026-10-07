# Сборка

Нужно: CMake ≥ 3.21, компилятор C++17/20, Qt 6 (или Qt 5.15) — Widgets, Network, Svg. toml++ и libsodium скачиваются сами (FetchContent).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build            # QT_QPA_PLATFORM=offscreen на машине без экрана
build/client/minimax              # рядом создаётся configs/ (client.toml, connect.ip)
```
Опции: `-DMINIMAX_QT_MAJOR=5|6`, `-DMINIMAX_PLATFORM_ID=<id>`, `-DMINIMAX_BUILD_TESTS=OFF`.

Разработка интерфейса без сервера: `minimax --demo` (примерные чаты), `--open settings|premium|stars|gifts|profile|drawer|archive|emoji|business|payment`,
`--auth login|register|captcha`, `--screenshot файл.png`. Сервер-заглушка: `python3 tools/mock_server.py --port 8080`.

## Что подставляется при сборке
* `logo.ico` в корне репозитория → иконка exe, окна и приложения (если файла нет — сборка идёт без иконки);
* `CaptchaAPI.cfg` → ключ капчи вшивается в бинарник; в архивы файл **не попадает** (`tools/package.py` и CI это проверяют);
* версия `04.10.26.0`, «Wharner APP», «Wharner Group», сайт — свойства exe на Windows (`client/win/app.rc.in`).

## Архивы GitHub Actions
`minimaxWinX64.zip`, `minimaxWinX86.zip`, `minimaxLinuxX64.tar.gz`, `minimaxLinuxX86.tar.gz`, `minimaxMacX64.tar.gz` — вкладка **Actions → Artifacts**; для тега `v*` ещё и в **Releases**.
