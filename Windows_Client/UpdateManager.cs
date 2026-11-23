using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;

namespace SolarPV.Client;

public partial class MainWindow
{
    private const string ReleaseApiUrl = "https://api.github.com/repos/sergeysikorskiycca-hash/Solar_PV/releases/tags/solar-pv-windows-demo";
    private const string UpdateAssetName = "Solar_PV_Windows_Portable_Update.zip";
    private static readonly HttpClient UpdateHttp = CreateUpdateClient();
    private bool _checkingUpdates;
    private MenuItem? _updateMenuItem;
    private static readonly string UpdateDiagnosticLogPath = Path.Combine(Path.GetTempPath(), "Solar_PV_Update_Diagnostic.log");
    private static readonly object UpdateDiagnosticLock = new();

    private static void WriteUpdateDiagnostic(string message, Exception? exception = null)
    {
        try
        {
            var entry = "[" + DateTimeOffset.Now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz") + "] " + message;
            if (exception is not null) entry += Environment.NewLine + exception + Environment.NewLine;
            lock (UpdateDiagnosticLock)
                File.AppendAllText(UpdateDiagnosticLogPath, entry + Environment.NewLine, new UTF8Encoding(false));
        }
        catch { }
    }

    private static HttpClient CreateUpdateClient()
    {
        var client = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd("Solar-PV-Windows-Client");
        client.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
        return client;
    }

    private async void CheckUpdates_Click(object sender, RoutedEventArgs e)
    {
        if (_checkingUpdates) return;
        _checkingUpdates = true;
        _updateMenuItem = sender as MenuItem;
        if (_updateMenuItem is not null) _updateMenuItem.IsEnabled = false;
        WriteUpdateDiagnostic("========== НАЧАЛО ПРОВЕРКИ ОБНОВЛЕНИЯ ==========");
        WriteUpdateDiagnostic("Версия приложения: " + (typeof(MainWindow).Assembly.GetName().Version?.ToString() ?? "не определена"));
        WriteUpdateDiagnostic("Путь приложения: " + (Environment.ProcessPath ?? "не определён"));
        WriteUpdateDiagnostic("Каталог приложения: " + AppContext.BaseDirectory);
        WriteUpdateDiagnostic("ОС: " + Environment.OSVersion + "; 64-bit OS: " + Environment.Is64BitOperatingSystem + "; 64-bit process: " + Environment.Is64BitProcess);
        try
        {
            WriteUpdateDiagnostic("Запрос информации о выпуске: " + ReleaseApiUrl);
            using var response = await UpdateHttp.GetAsync(ReleaseApiUrl);
            WriteUpdateDiagnostic("Ответ GitHub Release API: HTTP " + (int)response.StatusCode + " " + response.StatusCode);
            response.EnsureSuccessStatusCode();
            using var release = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            var root = release.RootElement;
            var body = root.TryGetProperty("body", out var bodyNode) ? bodyNode.GetString() ?? "" : "";
            var line = body.Split('\n').FirstOrDefault(x => x.TrimStart().StartsWith("Версия клиента:", StringComparison.OrdinalIgnoreCase));
            if (!Version.TryParse(line?.Split(':', 2).Last().Trim(), out var remoteVersion) || remoteVersion is null)
                throw new InvalidDataException("Не удалось определить версию опубликованной сборки.");
            WriteUpdateDiagnostic("Версия на сервере: " + remoteVersion);

            var currentVersion = typeof(MainWindow).Assembly.GetName().Version ?? new Version(0, 0, 0);
            WriteUpdateDiagnostic("Установленная версия: " + currentVersion);
            if (remoteVersion <= currentVersion)
            {
                WriteUpdateDiagnostic("Обновление не требуется: установленная версия актуальна.");
                MessageBox.Show("Установлена последняя версия Solar_PV (" + currentVersion.ToString(3) + ").", "Solar_PV — обновление");
                return;
            }

            var asset = root.GetProperty("assets").EnumerateArray()
                .FirstOrDefault(x => x.GetProperty("name").GetString() == UpdateAssetName);
            if (asset.ValueKind == JsonValueKind.Undefined)
                throw new InvalidDataException("Пакет обновления ещё не опубликован.");
            var digest = asset.GetProperty("digest").GetString();
            var url = asset.GetProperty("browser_download_url").GetString();
            if (string.IsNullOrWhiteSpace(digest) || !digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase) || string.IsNullOrWhiteSpace(url))
                throw new InvalidDataException("GitHub не предоставил корректную контрольную сумму SHA-256.");

            var answer = MessageBox.Show(
                "Доступна версия " + remoteVersion.ToString(3) + ". Установленная версия: " + currentVersion.ToString(3) + ".\n\nПрименить обновление?",
                "Solar_PV — обновление", MessageBoxButton.YesNo, MessageBoxImage.Information);
            WriteUpdateDiagnostic("Пользователь выбрал: " + answer);
            if (answer == MessageBoxResult.Yes)
                await InstallUpdateAsync(url, digest["sha256:".Length..], remoteVersion);
            else
                WriteUpdateDiagnostic("Установка отменена пользователем в окне подтверждения.");
        }
        catch (Exception ex)
        {
            WriteUpdateDiagnostic("ОШИБКА проверки/обновления.", ex);
            MessageBox.Show("Не удалось проверить или применить обновление.\n\n" + ex.Message + "\n\nЖурнал диагностики: " + UpdateDiagnosticLogPath,
                "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            WriteUpdateDiagnostic("Завершение обработчика проверки обновления.");
            _checkingUpdates = false;
            if (IsLoaded && _updateMenuItem is not null) _updateMenuItem.IsEnabled = true;
            _updateMenuItem = null;
        }
    }

    private async Task InstallUpdateAsync(string downloadUrl, string expectedHash, Version expectedVersion)
    {
        WriteUpdateDiagnostic("Начало установки версии " + expectedVersion + ". URL пакета: " + downloadUrl);
        var temp = Path.Combine(Path.GetTempPath(), "Solar_PV_Update");
        Directory.CreateDirectory(temp);
        WriteUpdateDiagnostic("Временный каталог: " + temp);
        var download = Path.Combine(temp, UpdateAssetName + ".download");
        var archive = Path.Combine(temp, UpdateAssetName);
        var scriptPath = Path.Combine(temp, "Apply_Solar_PV_Update.ps1");
        var exe = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(exe) || !File.Exists(exe))
            throw new InvalidOperationException("Не удалось определить путь запущенной программы.");

        WriteUpdateDiagnostic("Начало скачивания пакета: " + downloadUrl);
        using (var response = await UpdateHttp.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead))
        {
            WriteUpdateDiagnostic("Ответ скачивания: HTTP " + (int)response.StatusCode + " " + response.StatusCode + "; Content-Length: " + response.Content.Headers.ContentLength);
            response.EnsureSuccessStatusCode();
            await using var source = await response.Content.ReadAsStreamAsync();
            await using var target = new FileStream(download, FileMode.Create, FileAccess.Write, FileShare.None);
            await source.CopyToAsync(target);
        }

        WriteUpdateDiagnostic("Скачивание завершено. Размер файла: " + new FileInfo(download).Length + " байт.");
        string actualHash;
        await using (var file = File.OpenRead(download))
            actualHash = Convert.ToHexString(await SHA256.HashDataAsync(file));
        byte[] expectedBytes;
        try { expectedBytes = Convert.FromHexString(expectedHash); }
        catch (FormatException)
        {
            File.Delete(download);
            throw new InvalidDataException("GitHub вернул некорректную контрольную сумму.");
        }
        WriteUpdateDiagnostic("SHA-256 фактический: " + actualHash + "; ожидаемый: " + expectedHash.ToUpperInvariant());
        if (!CryptographicOperations.FixedTimeEquals(Convert.FromHexString(actualHash), expectedBytes))
        {
            File.Delete(download);
            throw new InvalidDataException("Контрольная сумма пакета не совпала. Обновление отменено.");
        }
        File.Move(download, archive, true);
        WriteUpdateDiagnostic("Контрольная сумма совпала. Архив сохранён: " + archive);

        static string Quote(string value) => "'" + value.Replace("'", "''") + "'";
        var lines = new[]
        {
            "$ErrorActionPreference = 'Stop'",
            "$targetProcessId = " + Environment.ProcessId,
            "$archive = " + Quote(archive),
            "$destination = " + Quote(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar)),
            "$app = " + Quote(exe),
            "$expected = [version]" + Quote(expectedVersion.ToString()),
            "$stage = Join-Path $env:TEMP ('Solar_PV_Stage_' + [guid]::NewGuid().ToString('N'))",
            "$backup = Join-Path $env:TEMP ('Solar_PV_Backup_' + [guid]::NewGuid().ToString('N'))",
            "$log = Join-Path $env:TEMP 'Solar_PV_Update_Diagnostic.log'",
            "$backupReady = $false",
            "$newProcess = $null",
            "$rollbackSucceeded = $false",
            "$step = 'Ожидание завершения программы'",
            "try {",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] PowerShell запущен. PID: ' + $PID + '; пользователь: ' + [Security.Principal.WindowsIdentity]::GetCurrent().Name + '; каталог: ' + $destination)",
            "  while (Get-Process -Id $targetProcessId -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 300 }",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] Основное приложение закрыто.')",
            "  $step = 'Распаковка пакета'",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] ' + $step)",
            "  Expand-Archive -LiteralPath $archive -DestinationPath $stage -Force",
            "  $newExe = Join-Path $stage 'Solar_PV.exe'",
            "  $stagedVersionFile = Join-Path $stage 'version.txt'",
            "  if (-not (Test-Path -LiteralPath $newExe -PathType Leaf)) { throw 'В корне архива не найден Solar_PV.exe.' }",
            "  if (-not (Test-Path -LiteralPath $stagedVersionFile -PathType Leaf)) { throw 'В пакете отсутствует version.txt. Обновление отменено.' }",
            "  $stagedVersionText = (Get-Content -LiteralPath $stagedVersionFile -Raw).Trim()",
            "  [version]$parsedStagedVersion = [version]'0.0'",
            "  if (-not [version]::TryParse($stagedVersionText, [ref]$parsedStagedVersion) -or $parsedStagedVersion -ne $expected) { throw 'Версия в version.txt не совпадает с версией выпуска.' }",
            "  if ((Get-Item -LiteralPath $newExe).Length -lt 1000000) { throw 'Размер EXE в архиве меньше ожидаемого.' }",
            "  if (-not (Test-Path -LiteralPath $app -PathType Leaf)) { throw 'Не найден установленный Solar_PV.exe.' }",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] Проверки архива и версии пройдены.')",
            "  $step = 'Создание резервной копии'",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] ' + $step)",
            "  New-Item -ItemType Directory -Path $backup -Force | Out-Null",
            "  Copy-Item -Path (Join-Path $destination '*') -Destination $backup -Recurse -Force",
            "  if (-not (Test-Path -LiteralPath (Join-Path $backup 'Solar_PV.exe') -PathType Leaf)) { throw 'Не удалось создать резервную копию программы.' }",
            "  $backupReady = $true",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] Резервная копия создана: ' + $backup)",
            "  $step = 'Копирование новой версии'",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] ' + $step)",
            "  Copy-Item -Path (Join-Path $stage '*') -Destination $destination -Recurse -Force",
            "  $installed = Join-Path $destination 'Solar_PV.exe'",
            "  $installedVersionFile = Join-Path $destination 'version.txt'",
            "  if (-not (Test-Path -LiteralPath $installed -PathType Leaf) -or (Get-Item -LiteralPath $installed).Length -ne (Get-Item -LiteralPath $newExe).Length) { throw 'Проверка установленного EXE не пройдена.' }",
            "  if ((Get-Content -LiteralPath $installedVersionFile -Raw).Trim() -ne $expected.ToString()) { throw 'Проверка установленной версии не пройдена.' }",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] Файлы скопированы, проверка EXE и version.txt пройдена.')",
            "  $step = 'Запуск и проверка новой версии'",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] ' + $step)",
            "  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()",
            "  $principal = New-Object Security.Principal.WindowsPrincipal($identity)",
            "  if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {",
            "    $shell = New-Object -ComObject Shell.Application",
            "    $shell.ShellExecute($installed)",
            "    Start-Sleep -Seconds 5",
            "    $newProcess = Get-Process -Name 'Solar_PV' -ErrorAction SilentlyContinue | Select-Object -First 1",
            "    if (-not $newProcess) { throw 'Не удалось запустить программу после обновления.' }",
            "  } else {",
            "    $newProcess = Start-Process -FilePath $installed -PassThru",
            "    Start-Sleep -Seconds 5",
            "    $newProcess.Refresh()",
            "    if ($newProcess.HasExited) { throw 'Новая версия завершилась сразу после запуска. Выполняется откат.' }",
            "  }",
            "  Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] Установка успешно завершена. PID новой программы: ' + $newProcess.Id)",
            "  Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue",
            "  Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue",
            "  Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue",
            "  Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue",
            "} catch {",
            "  ('[' + (Get-Date -Format s) + '] Ошибка на этапе: ' + $step) | Add-Content -LiteralPath $log",
            "  ($_ | Format-List * -Force | Out-String) | Add-Content -LiteralPath $log",
            "  try { Stop-Transcript | Out-Null } catch { }",
            "  if ($newProcess -and -not $newProcess.HasExited) { Stop-Process -Id $newProcess.Id -Force -ErrorAction SilentlyContinue }",
            "  if ($backupReady) {",
            "    Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] Начат откат из резервной копии: ' + $backup)",
            "    try {",
            "      Get-ChildItem -LiteralPath $destination -Force | Remove-Item -Recurse -Force",
            "      Copy-Item -Path (Join-Path $backup '*') -Destination $destination -Recurse -Force",
            "      if (-not (Test-Path -LiteralPath (Join-Path $destination 'Solar_PV.exe') -PathType Leaf)) { throw 'После копирования резервной копии EXE не найден.' }",
            "      $rollbackSucceeded = $true",
            "      Add-Content -LiteralPath $log -Value ('[' + (Get-Date -Format s) + '] Откат успешно завершён.')",
            "    } catch { ($_ | Out-String) | Add-Content -LiteralPath $log }",
            "  } else { $rollbackSucceeded = $true }",
            "  if ($rollbackSucceeded -and (Test-Path -LiteralPath $app -PathType Leaf)) {",
            "    try { Start-Process -FilePath $app } catch { ($_ | Out-String) | Add-Content -LiteralPath $log }",
            "  }",
            "  Add-Type -AssemblyName PresentationFramework",
            "  $message = if ($rollbackSucceeded) { 'Обновление не удалось. Предыдущая версия восстановлена. Подробности: ' + $log } else { 'Обновление не удалось, и автоматическое восстановление завершилось ошибкой. Не запускайте программу повторно до проверки файлов. Подробности: ' + $log }",
            "  [System.Windows.MessageBox]::Show($message, 'Solar_PV — ошибка обновления') | Out-Null",
            "}"
        };
        await File.WriteAllTextAsync(scriptPath, string.Join(Environment.NewLine, lines), new UTF8Encoding(false));
        WriteUpdateDiagnostic("PowerShell-скрипт создан: " + scriptPath + "; размер: " + new FileInfo(scriptPath).Length + " байт.");

        var needsElevation = false;
        var permissionProbe = Path.Combine(AppContext.BaseDirectory, ".solar-pv-write-" + Guid.NewGuid().ToString("N"));
        try
        {
            await File.WriteAllTextAsync(permissionProbe, "test");
            File.Delete(permissionProbe);
            WriteUpdateDiagnostic("Проверка прав: запись в каталог приложения разрешена без повышения прав.");
        }
        catch (UnauthorizedAccessException ex)
        {
            needsElevation = true;
            WriteUpdateDiagnostic("Проверка прав: запись в каталог приложения запрещена. Требуются права администратора.", ex);
        }
        finally
        {
            if (File.Exists(permissionProbe))
                File.Delete(permissionProbe);
        }

        WriteUpdateDiagnostic("Проверка необходимости UAC завершена. Требуется повышение прав: " + needsElevation);
        var startInfo = new ProcessStartInfo
        {
            FileName = "powershell.exe",
            Arguments = "-NoProfile -ExecutionPolicy Bypass -NoExit -File \"" + scriptPath + "\"",
            UseShellExecute = true,
            WindowStyle = ProcessWindowStyle.Normal
        };
        if (needsElevation)
            startInfo.Verb = "runas";

        WriteUpdateDiagnostic("Запуск PowerShell. Файл: " + startInfo.FileName + "; аргументы: " + startInfo.Arguments + "; Verb: " + (startInfo.Verb.Length == 0 ? "(пусто)" : startInfo.Verb) + "; UseShellExecute: " + startInfo.UseShellExecute);
        Process? updater;
        try
        {
            updater = Process.Start(startInfo);
        }
        catch (System.ComponentModel.Win32Exception ex) when (ex.NativeErrorCode == 1223)
        {
            WriteUpdateDiagnostic("Запрос UAC отменён пользователем.", ex);
            throw new OperationCanceledException("Запуск обновления с правами администратора отменён. Программа не закрыта.", ex);
        }
        catch (Exception ex)
        {
            WriteUpdateDiagnostic("ОШИБКА запуска PowerShell.", ex);
            throw;
        }
        if (updater is null)
        {
            WriteUpdateDiagnostic("Process.Start вернул null: Windows не запустила PowerShell.");
            throw new InvalidOperationException("Windows не запустила процесс обновления.");
        }
        WriteUpdateDiagnostic("PowerShell запущен. PID: " + updater.Id + "; время запуска: " + updater.StartTime.ToString("O"));
        WriteUpdateDiagnostic("Передача управления обновляющему скрипту. Завершение основного приложения.");
        Application.Current.Shutdown();
    }
}
