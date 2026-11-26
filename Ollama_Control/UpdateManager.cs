using System.Diagnostics;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;

namespace OllamaControl;

internal static class UpdateManager
{
    private const string ReleaseApi = "https://api.github.com/repos/sergeysikorskiycca-hash/Solar_PV/releases/tags/ollama-control-windows";
    private const string AssetName = "OllamaControl-win-x64.zip";
    private static readonly HttpClient Http = CreateClient();

    private static HttpClient CreateClient()
    {
        var c = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        c.DefaultRequestHeaders.UserAgent.ParseAdd("OllamaControl-Updater");
        c.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
        return c;
    }

    public static async Task CheckAsync(IWin32Window owner)
    {
        try
        {
            using var response = await Http.GetAsync(ReleaseApi);
            if (response.StatusCode == System.Net.HttpStatusCode.NotFound)
            {
                MessageBox.Show(owner, "Обновлений пока нет.", "Ollama Control");
                return;
            }
            response.EnsureSuccessStatusCode();
            using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
            var root = json.RootElement;
            var notes = root.GetProperty("body").GetString() ?? "";
            var line = notes.Split('\n').FirstOrDefault(x => x.StartsWith("Версия программы:", StringComparison.OrdinalIgnoreCase));
            if (!Version.TryParse(line?.Split(':', 2).Last().Trim(), out var remote)) throw new InvalidDataException("В релизе не указана версия.");
            var current = typeof(UpdateManager).Assembly.GetName().Version ?? new Version(0,0,0);
            if (remote <= current)
            {
                MessageBox.Show(owner, "Установлена последняя версия " + current.ToString(3) + ".", "Ollama Control");
                return;
            }
            var asset = root.GetProperty("assets").EnumerateArray().FirstOrDefault(x => x.GetProperty("name").GetString() == AssetName);
            if (asset.ValueKind == JsonValueKind.Undefined) throw new InvalidDataException("ZIP-пакет не найден.");
            var digest = asset.GetProperty("digest").GetString();
            var url = asset.GetProperty("browser_download_url").GetString();
            if (string.IsNullOrWhiteSpace(digest) || !digest.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase) || string.IsNullOrWhiteSpace(url))
                throw new InvalidDataException("В релизе отсутствует SHA-256 или ссылка на пакет.");
            if (MessageBox.Show(owner, "Доступна версия " + remote.ToString(3) + ". Установить обновление?", "Ollama Control", MessageBoxButtons.YesNo, MessageBoxIcon.Information) != DialogResult.Yes) return;
            await InstallAsync(url, digest[7..], remote);
        }
        catch (Exception ex)
        {
            MessageBox.Show(owner, "Ошибка обновления: " + ex.Message, "Ollama Control", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
    }

    private static async Task InstallAsync(string url, string expectedHash, Version version)
    {
        var app = Environment.ProcessPath ?? throw new InvalidOperationException("Не найден путь EXE.");
        var temp = Path.Combine(Path.GetTempPath(), "OllamaControl_Update");
        Directory.CreateDirectory(temp);
        var zip = Path.Combine(temp, AssetName);
        var script = Path.Combine(temp, "ApplyUpdate.ps1");
        using (var response = await Http.GetAsync(url, HttpCompletionOption.ResponseHeadersRead))
        {
            response.EnsureSuccessStatusCode();
            await using var source = await response.Content.ReadAsStreamAsync();
            await using var target = File.Create(zip);
            await source.CopyToAsync(target);
        }
        await using (var stream = File.OpenRead(zip))
        {
            var actual = Convert.ToHexString(await SHA256.HashDataAsync(stream));
            if (!CryptographicOperations.FixedTimeEquals(Convert.FromHexString(actual), Convert.FromHexString(expectedHash)))
                throw new InvalidDataException("Контрольная сумма SHA-256 не совпала. Обновление отменено.");
        }
        static string Q(string s) => "'" + s.Replace("'", "''") + "'";
        var lines = new[]
        {
            "$ErrorActionPreference='Stop'",
            "$pidToWait=" + Environment.ProcessId,
            "$zip=" + Q(zip),
            "$dest=" + Q(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar)),
            "$app=" + Q(app),
            "$version=" + Q(version.ToString()),
            "$stage=Join-Path $env:TEMP ('OllamaControl_Stage_'+[guid]::NewGuid().ToString('N'))",
            "$backup=Join-Path $env:TEMP ('OllamaControl_Backup_'+[guid]::NewGuid().ToString('N'))",
            "try {",
            " while(Get-Process -Id $pidToWait -ErrorAction SilentlyContinue){Start-Sleep -Milliseconds 300}",
            " Expand-Archive -LiteralPath $zip -DestinationPath $stage -Force",
            " if(-not(Test-Path (Join-Path $stage 'OllamaControl.exe'))){throw 'EXE отсутствует в пакете'}",
            " if((Get-Content (Join-Path $stage 'version.txt') -Raw).Trim() -ne $version){throw 'Версия пакета не совпадает'}",
            " New-Item -ItemType Directory -Path $backup -Force | Out-Null",
            " Copy-Item -Path (Join-Path $dest '*') -Destination $backup -Recurse -Force",
            " Copy-Item -Path (Join-Path $stage '*') -Destination $dest -Recurse -Force",
            " if((Get-Item (Join-Path $dest 'OllamaControl.exe')).Length -ne (Get-Item (Join-Path $stage 'OllamaControl.exe')).Length){throw 'Проверка EXE после копирования не пройдена'}",
            " Start-Process -FilePath (Join-Path $dest 'OllamaControl.exe')",
            " Remove-Item $backup -Recurse -Force -ErrorAction SilentlyContinue",
            "} catch {",
            " if(Test-Path $backup){Get-ChildItem $dest -Force | Remove-Item -Recurse -Force; Copy-Item (Join-Path $backup '*') $dest -Recurse -Force}",
            " Add-Type -AssemblyName PresentationFramework",
            " [System.Windows.MessageBox]::Show('Обновление не удалось. Предыдущая версия восстановлена. '+$_,'Ollama Control') | Out-Null",
            "}",
            "Remove-Item $zip -Force -ErrorAction SilentlyContinue",
            "Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue",
            "Remove-Item $PSCommandPath -Force -ErrorAction SilentlyContinue"
        };
        await File.WriteAllTextAsync(script, string.Join(Environment.NewLine, lines), new System.Text.UTF8Encoding(true));
        Process.Start(new ProcessStartInfo("powershell.exe", "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"") { UseShellExecute = true, WindowStyle = ProcessWindowStyle.Hidden });
        Application.Exit();
    }
}