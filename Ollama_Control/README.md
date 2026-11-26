# Ollama Control

Русскоязычная Windows Forms программа для управления локальной Ollama.

- Проверка доступности и версии Ollama.
- Списки установленных и загруженных моделей.
- Загрузка и выгрузка моделей.
- Запуск OpenCode.
- Журнал действий и настраиваемая цветовая схема.
- Информационный текст можно выделять и копировать мышью, через Ctrl+C или контекстное меню.
- При первом запуске создаётся ярлык на рабочем столе, если его ещё нет.
- Настройки сохраняются в %APPDATA%\OllamaControl.

Сборка на Windows с .NET 8 SDK:
```powershell
dotnet publish OllamaControl.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o publish
```

Переключение модели внутри OpenCode и удаление моделей не реализованы.
