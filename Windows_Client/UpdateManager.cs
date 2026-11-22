using System.IO;
using System.Diagnostics;
using System.Net.Http;
using System.Security.Cryptography;
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
        if (_updateMenuItem is not null)
        {
            _updateMenuItem.IsEnabled = false;
            _updateMenuItem.Header = "Проверка…";
        }
        try
        {
            using var response = await UpdateHttp.GetAsync(ReleaseApiUrl);
            response.EnsureSuccessStatusCode();
            using var release = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            var root = release.RootElement;
            var body = root.TryGetProperty("body", out var bodyNode) ? bodyNode.GetString() ?? "" : "";
            var versionLine = body.Split('\n').FirstOrDefault(line => line.TrimStart().StartsWith("Версия клиента:", StringComparison.OrdinalIgnoreCase));
            var versionText = versionLine?.Split(':', 2).Last().Trim();

            if (!Version.TryParse(versionText, out var remoteVersion))
            {
                MessageBox.Show("Не удалось определить версию опубликованной сборки. Обновление пока недоступно.",
                    "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            var currentVersion = typeof(MainWindow).Assembly.GetName().Version ?? new Version(0, 0, 0);
            if (remoteVersion <= currentVersion)
            {
                MessageBox.Show($"Установлена последняя версия Solar_PV ({currentVersion.ToString(3)}).",
                    "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            var asset = root.GetProperty("assets").EnumerateArray()
                .FirstOrDefault(item => item.TryGetProperty("name", out var name) && name.GetString() == UpdateAssetName);
            if (asset.ValueKind == JsonValueKind.Undefined)
            {
                MessageBox.Show("Новая версия найдена, но пакет обновления ещё не опубликован. Попробуйте позже.",
                    "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            var digest = asset.TryGetProperty("digest", out var digestNode) ? digestNode.GetString() : null;
            var downloadUrl = asset.GetProperty("browser_download_url").GetString();
            if (string.IsNullOrWhiteSpace(digest) || !digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase) ||
                string.IsNullOrWhiteSpace(downloadUrl))
            {
                MessageBox.Show("GitHub не предоставил контрольную сумму пакета. В целях безопасности обновление отменено.",
                    "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            var answer = MessageBox.Show(
                $"Доступна новая версия Solar_PV: {remoteVersion.ToString(3)}.\n\nУстановленная версия: {currentVersion.ToString(3)}.\n\nСкачать и применить обновление без повторного запуска установщика?",
                "Solar_PV — найдено обновление", MessageBoxButton.YesNo, MessageBoxImage.Information);
            if (answer == MessageBoxResult.Yes)
                await InstallUpdateAsync(downloadUrl, digest["sha256:".Length..]);
        }
        catch (Exception ex)
        {
            MessageBox.Show("Не удалось проверить обновления или загрузить пакет.\n\n" + ex.Message,
                "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            _checkingUpdates = false;
            if (IsLoaded && _updateMenuItem is not null)
            {
                _updateMenuItem.IsEnabled = true;
                _updateMenuItem.Header = "Проверить обновления…";
            }
            _updateMenuItem = null;
        }
    }

    private async Task InstallUpdateAsync(string downloadUrl, string expectedHash)
    {
        var tempDirectory = Path.Combine(Path.GetTempPath(), "Solar_PV_Update");
        Directory.CreateDirectory(tempDirectory);
        var downloadPath = Path.Combine(tempDirectory, UpdateAssetName + ".download");
        var archivePath = Path.Combine(tempDirectory, UpdateAssetName);
        var scriptPath = Path.Combine(tempDirectory, "Apply_Solar_PV_Update.ps1");
        var executablePath = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(executablePath) || !File.Exists(executablePath))
            throw new InvalidOperationException("Не удалось определить путь запущенной программы.");

        var applicationDirectory = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar);
        if (_updateMenuItem is not null)
            _updateMenuItem.Header = "Загрузка обновления…";
        using (var response = await UpdateHttp.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead))
        {
            response.EnsureSuccessStatusCode();
            await using var source = await response.Content.ReadAsStreamAsync();
            await using var destination = new FileStream(downloadPath, FileMode.Create, FileAccess.Write, FileShare.None);
            await source.CopyToAsync(destination);
        }

        string actualHash;
        await using (var downloaded = File.OpenRead(downloadPath))
            actualHash = Convert.ToHexString(await SHA256.HashDataAsync(downloaded));

        if (!CryptographicOperations.FixedTimeEquals(Convert.FromHexString(actualHash), Convert.FromHexString(expectedHash)))
        {
            File.Delete(downloadPath);
            throw new InvalidDataException("Контрольная сумма пакета не совпала. Файл удалён, обновление не выполнялось.");
        }

        File.Move(downloadPath, archivePath, true);
        static string PsQuote(string value) => "'" + value.Replace("'", "''") + "'";
        var script = """
            $ErrorActionPreference = 'Stop'
            $processIdToWait = __PROCESS_ID__
            $archive = __ARCHIVE_PATH__
            $destination = __APP_DIRECTORY__
            $app = __APP_PATH__
            $staging = Join-Path $env:TEMP ('Solar_PV_Stage_' + [guid]::NewGuid().ToString('N'))
            $log = Join-Path $env:TEMP 'Solar_PV_Update.log'
            try {
                while (Get-Process -Id $processIdToWait -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 500 }
                Expand-Archive -LiteralPath $archive -DestinationPath $staging -Force
                Copy-Item -Path (Join-Path $staging '*') -Destination $destination -Recurse -Force
                Start-Process -FilePath $app
                Remove-Item -LiteralPath $archive -Force -ErrorAction SilentlyContinue
                Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
                Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
            }
            catch {
                $_ | Out-String | Set-Content -LiteralPath $log
                Add-Type -AssemblyName PresentationFramework
                [System.Windows.MessageBox]::Show("Не удалось применить обновление. Подробности: $log", "Solar_PV — ошибка обновления") | Out-Null
                if (Test-Path -LiteralPath $app) { Start-Process -FilePath $app }
            }
            """;
        script = script.Replace("__PROCESS_ID__", Environment.ProcessId.ToString())
            .Replace("__ARCHIVE_PATH__", PsQuote(archivePath))
            .Replace("__APP_DIRECTORY__", PsQuote(applicationDirectory))
            .Replace("__APP_PATH__", PsQuote(executablePath));
        await File.WriteAllTextAsync(scriptPath, script);

        Process.Start(new ProcessStartInfo
        {
            FileName = "powershell.exe",
            Arguments = $"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"{scriptPath}\"",
            UseShellExecute = true,
            WindowStyle = ProcessWindowStyle.Hidden
        });
        Application.Current.Shutdown();
    }
}