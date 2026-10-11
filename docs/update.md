# Автообновление (`update` / `update.exe`)

Ярлык MINI max указывает на **update**. Он без окна:
1. читает `configs/connect.ip`, скачивает `/update/<platform>/manifest.json` и `manifest.sig`;
2. проверяет подпись Ed25519 открытым ключом `configs/update_public.key` (нет ключа/плохая подпись → обновления игнорируются);
3. сверяет SHA-256 файлов; если `revision` изменилась или файлы отличаются — скачивает изменённые, проверяет хэши и заменяет;
4. запускает `minimax` (если сервер недоступен — просто запускает `minimax`).

`configs/connect.ip` никогда не перезаписывается. Журнал: `update.log` в папке данных приложения.

## Что делает сервер («Загрузить обновление»)
Папка на сервере: `MmServer/<platform>/…` (`minimaxWinX64`, `minimaxWinX86`, `minimaxLinuxX64`, `minimaxLinuxX86`, `minimaxMacX64`) — это распакованный архив из GitHub.
При загрузке сервер заменяет папку, строит `manifest.json` (список файлов + SHA-256 + новая `revision`), подписывает его закрытым ключом и пишет событие в свой журнал.
Готовые реализации: `tools/make_manifest.py` (Python) и `tools/server-sample/UpdateManifest.cs` (C#, NSec).

## Ключи
```
python3 tools/make_manifest.py --gen-keys keys/
```
`keys/update_public.key` → в репозиторий как `configs/update_public.key` (до сборки). `update_private.key` — только на сервере.
