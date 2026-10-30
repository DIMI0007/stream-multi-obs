# Происхождение и изменения

Stream Multi основан на [Aitum Multistream](https://github.com/Aitum/obs-aitum-multistream), авторы Aitum и Exeldro. Исходный код опубликован по GPL-2.0. Лицензия сохранена в файле [LICENSE](LICENSE); история оригинального репозитория сохранена в Git.

Адаптация DMITRII от 30 октября 2026 года:

- `multistream.cpp`, `multistream.hpp`: отдельное название панели, палитра Stream Suite, интерфейс только для обычного холста, отдельный канал OBS WebSocket, перенос горизонтальных настроек, отключение обращения к серверу обновлений Aitum.
- `config-dialog.cpp`, `output-dialog.cpp`, `data/locale/*.ini`: оформление окон, названия и тексты; вертикальные настройки скрыты.
- `buildspec.json`, `data/manifest.json`, `resources.qrc`, `media/stream-multi.svg`: отдельная идентичность и значок Stream Multi.
- `README.md`, `NOTICE.md`: описание адаптации и инструкции.

Авторы оригинала сохраняют права на свой код. DMITRII является автором перечисленных изменений.
