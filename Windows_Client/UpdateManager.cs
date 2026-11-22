using System.IO;
using System.Diagnostics;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;
using System.Windows;

namespace SolarPV.Client;

public partial class MainWindow
{
    private const string ReleaseApiUrl = "https://api.github.com/repos/sergeysikorskiycca-hash/Solar_PV/releases/tags/solar-pv-windows-demo";
    private const string InstallerAssetName = "Solar_PV_Windows_Demo_Setup.exe";
    private static readonly HttpClient UpdateHttp = CreateUpdateClient();
    private bool _checkingUpdates;

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
        UpdateButton.IsEnabled = false;
        UpdateButton.Content = "Проверка…";
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
                .FirstOrDefault(item => item.TryGetProperty("name", out var name) && name.GetString() == InstallerAssetName);
            if (asset.ValueKind == JsonValueKind.Undefined)
            {
                MessageBox.Show("Новая версия найдена, но установочный файл пока не опубликован. Попробуйте позже.",
                    "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            var digest = asset.TryGetProperty("digest", out var digestNode) ? digestNode.GetString() : null;
            var downloadUrl = asset.GetProperty("browser_download_url").GetString();
            if (string.IsNullOrWhiteSpace(digest) || !digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase) ||
                string.IsNullOrWhiteSpace(downloadUrl))
            {
                MessageBox.Show("GitHub не предоставил контрольную сумму установщика. В целях безопасности установка отменена.",
                    "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            var answer = MessageBox.Show(
                $"Доступна новая версия Solar_PV: {remoteVersion.ToString(3)}.\n\nУстановленная версия: {currentVersion.ToString(3)}.\n\nСкачать и установить обновление автоматически?",
                "Solar_PV — найдено обновление", MessageBoxButton.YesNo, MessageBoxImage.Information);
            if (answer == MessageBoxResult.Yes)
                await InstallUpdateAsync(downloadUrl, digest["sha256:".Length..]);
        }
        catch (Exception ex)
        {
            MessageBox.Show("Не удалось проверить обновления или загрузить установщик.\n\n" + ex.Message,
                "Solar_PV — обновление", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
        finally
        {
            _checkingUpdates = false;
            if (IsLoaded)
            {
                UpdateButton.IsEnabled = true;
                UpdateButton.Content = "Проверить обновления";
            }
        }
    }

    private async Task InstallUpdateAsync(string downloadUrl, string expectedHash)
    {
        var tempDirectory = Path.Combine(Path.GetTempPath(), "Solar_PV_Update");
        Directory.CreateDirectory(tempDirectory);
        var installerPath = Path.Combine(tempDirectory, InstallerAssetName + ".download");
        var finalInstallerPath = Path.Combine(tempDirectory, InstallerAssetName);
        var scriptPath = Path.Combine(tempDirectory, "Install_Solar_PV_Update.ps1");
        var executablePath = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(executablePath) || !File.Exists(executablePath))
            throw new InvalidOperationException("Не удалось определить путь установленной программы.");

        UpdateButton.Content = "Загрузка обновления…";
        using (var response = await UpdateHttp.GetAsync(downloadUrl, HttpCompletionOption.ResponseHeadersRead))
        {
            response.EnsureSuccessStatusCode();
            await using var source = await response.Content.ReadAsStreamAsync();
            await using var destination = new FileStream(installerPath, FileMode.Create, FileAccess.Write, FileShare.None);
            await source.CopyToAsync(destination);
        }

        string actualHash;
        await using (var downloaded = File.OpenRead(installerPath))
            actualHash = Convert.ToHexString(await SHA256.HashDataAsync(downloaded));

        if (!CryptographicOperations.FixedTimeEquals(Convert.FromHexString(actualHash), Convert.FromHexString(expectedHash)))
        {
            File.Delete(installerPath);
            throw new InvalidDataException("Контрольная сумма установщика не совпала. Файл удалён, установка не выполнялась.");
        }

        File.Move(installerPath, finalInstallerPath, true);
        static string PsQuote(string value) => "'" + value.Replace("'", "''") + "'";
        var script = """
            $processIdToWait = __PROCESS_ID__
            while (Get-Process -Id $processIdToWait -ErrorAction SilentlyContinue) { Start-Sleep -Seconds 1 }
            $installer = Start-Process -FilePath '__INSTALLER_PATH__' -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CLOSEAPPLICATIONS' -Wait -PassThru
            if ($installer.ExitCode -eq 0) { Start-Process -FilePath '__APP_PATH__' }
            """;
        script = script.Replace("__PROCESS_ID__", Environment.ProcessId.ToString())
            .Replace("'__INSTALLER_PATH__'", PsQuote(finalInstallerPath))
            .Replace("'__APP_PATH__'", PsQuote(executablePath));
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
