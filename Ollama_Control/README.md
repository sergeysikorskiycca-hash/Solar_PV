# Ollama Control

Русскоязычная Windows Forms программа для управления локальным Ollama.

- Проверка версии и доступности API.
- Список установленных и загруженных моделей.
- Загрузка и выгрузка моделей.
- Выгрузка всех моделей с подтверждением.
- Запуск OpenCode при наличии команды opencode в PATH.
- Журнал, настройка цветовых ролей, сохранение настроек в %APPDATA%\OllamaControl.
- Отдельный GitHub Release и автообновление (Windows x64).

Сборка на Windows с .NET 8 SDK:
```powershell
dotnet publish OllamaControl.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true -p:IncludeNativeLibrariesForSelfExtract=true -o publish
```

Переключение модели внутри OpenCode не реализовано. Удаление моделей не предусмотрено. Приложение и обновлятор пока требуют сборочной и функциональной проверки на Windows.