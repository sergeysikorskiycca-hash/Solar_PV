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
        try
        {
            using var response = await UpdateHttp.GetAsync(ReleaseApiUrl);
            response.EnsureSuccessStatusCode();
            using var release = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            var root = release.RootElement;
            var body = root.TryGetProperty("body", out var bodyNode) ? bodyNode.GetString() ?? "" : "";
            var line = body.Split('\n').FirstOrDefault(x => x.TrimStart().StartsWith("Версия клиента:", StringComparison.OrdinalIgnoreCase));
            if (!Version.TryParse(line?.Split(':', 2).Last().Trim(), out var remoteVersion) || remoteVersion is null)
                throw new InvalidDataException("Не удалось определить версию опубликованной сборки.");

            var currentVersion = typeof(MainWindow).Assembly.GetName().Version ?? new Version(0, 0, 0);
            if (remoteVersion <= currentVersion)
            {
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
            if (answer == MessageBoxResult.Yes)
                await InstallUpdateAsync(url, digest["sha256:".Length..], remoteVersion);
        }
        catch (Exception ex)
        {
            MessageBox.Show("Не удалось проверить или применить обновление.\n\n" + ex.Message,
                "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            _checkingUpdates = false;
            if (IsLoaded && _updateMenuItem is not null) _updateMenuItem.IsEnabled = true;
            _updateMenuItem = null;
        }
    }

    private async Task InstallUpdateAsync(string downloadUrl, string expectedHash, Version expectedVersion)
    {
        var temp = Path.Combine(Path.GetTempPath(), "Solar_PV_Update");
        Directory.CreateDirectory(temp);
        var download = Path.Combine(temp, UpdateAssetName + ".download");
        var archive = Path.Combine(temp, UpdateAssetName);
        var scriptPath = Path.Combine(temp, "Apply_Solar_PV_Update.ps1");
        var exe = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(exe) || !File.Exists(exe))
            throw new InvalidOperationException("Не удалось определить путь запущенной программы.");

        using (var response = await UpdateHttp.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead))
        {
            response.EnsureSuccessStatusCode();
            await using var source = await response.Content.ReadAsStreamAsync();
            await using var target = new FileStream(download, FileMode.Create, FileAccess.Write, FileShare.None);
            await source.CopyToAsync(target);
        }

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
        if (!CryptographicOperations.FixedTimeEquals(Convert.FromHexString(actualHash), expectedBytes))
        {
            File.Delete(download);
            throw new InvalidDataException("Контрольная сумма пакета не совпала. Обновление отменено.");
        }
        File.Move(download, archive, true);

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
            "$log = Join-Path $env:TEMP 'Solar_PV_Update.log'",
            "$backupReady = $false",
            "$newProcess = $null",
            "$rollbackSucceeded = $false",
            "try {",
            "  while (Get-Process -Id $targetProcessId -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 300 }",
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
            "  New-Item -ItemType Directory -Path $backup -Force | Out-Null",
            "  Copy-Item -Path (Join-Path $destination '*') -Destination $backup -Recurse -Force",
            "  if (-not (Test-Path -LiteralPath (Join-Path $backup 'Solar_PV.exe') -PathType Leaf)) { throw 'Не удалось создать резервную копию программы.' }",
            "  $backupReady = $true",
            "  Copy-Item -Path (Join-Path $stage '*') -Destination $destination -Recurse -Force",
            "  $installed = Join-Path $destination 'Solar_PV.exe'",
            "  $installedVersionFile = Join-Path $destination 'version.txt'",
            "  if (-not (Test-Path -LiteralPath $installed -PathType Leaf) -or (Get-Item -LiteralPath $installed).Length -ne (Get-Item -LiteralPath $newExe).Length) { throw 'Проверка установленного EXE не пройдена.' }",
            "  if ((Get-Content -LiteralPath $installedVersionFile -Raw).Trim() -ne $expected.ToString()) { throw 'Проверка установленной версии не пройдена.' }",
            "  $newProcess = Start-Process -FilePath $installed -PassThru",
            "  Start-Sleep -Seconds 5",
            "  $newProcess.Refresh()",
            "  if ($newProcess.HasExited) { throw 'Новая версия завершилась сразу после запуска. Выполняется откат.' }",
            "  Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue",
            "  Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue",
            "  Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue",
            "  Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue",
            "} catch {",
            "  $_ | Out-String | Set-Content -LiteralPath $log -Encoding UTF8",
            "  if ($newProcess -and -not $newProcess.HasExited) { Stop-Process -Id $newProcess.Id -Force -ErrorAction SilentlyContinue }",
            "  if ($backupReady) {",
            "    try {",
            "      Get-ChildItem -LiteralPath $destination -Force | Remove-Item -Recurse -Force",
            "      Copy-Item -Path (Join-Path $backup '*') -Destination $destination -Recurse -Force",
            "      if (-not (Test-Path -LiteralPath (Join-Path $destination 'Solar_PV.exe') -PathType Leaf)) { throw 'После копирования резервной копии EXE не найден.' }",
            "      $rollbackSucceeded = $true",
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

        var updater = Process.Start(new ProcessStartInfo
        {
            FileName = "powershell.exe",
            Arguments = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + scriptPath + "\"",
            UseShellExecute = true,
            WindowStyle = ProcessWindowStyle.Hidden
        });
        if (updater is null) throw new InvalidOperationException("Windows не запустила процесс обновления.");
        Application.Current.Shutdown();
    }
}
