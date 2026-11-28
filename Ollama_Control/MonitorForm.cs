using System.Diagnostics;
using System.Net.Http;
using System.Net;
using System.Net.NetworkInformation;
using System.Text.Json;

namespace OllamaControl;

internal sealed class MonitorForm : Form
{
    private readonly string ollamaUrl;
    private readonly AppSettings cfg;
    private readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(4) };
    private readonly TextBox report = new()
    {
        Dock = DockStyle.Fill,
        Multiline = true,
        ReadOnly = true,
        ScrollBars = ScrollBars.Both,
        WordWrap = false,
        Font = new Font("Consolas", 10)
    };
    private readonly Button refreshButton = new() { Text = "Обновить", AutoSize = true };
    private readonly System.Windows.Forms.Timer timer = new() { Interval = 3000 };
    private readonly Dictionary<int, (TimeSpan Cpu, DateTime Sample)> cpuSamples = new();
    private bool refreshing;

    public MonitorForm(string apiUrl, AppSettings settings)
    {
        ollamaUrl = apiUrl.TrimEnd('/');
        cfg = settings;
        Text = "Мониторинг ИИ";
        Width = 1000;
        Height = 760;
        MinimumSize = new Size(700, 480);
        StartPosition = FormStartPosition.CenterParent;

        var top = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(8) };
        top.Controls.Add(refreshButton);
        top.Controls.Add(new Label
        {
            Text = "Автообновление каждые 3 секунды",
            AutoSize = true,
            Padding = new Padding(8, 7, 0, 0)
        });
        refreshButton.FlatStyle = FlatStyle.Flat;
        refreshButton.BackColor = ColorTranslator.FromHtml(cfg.ButtonBackground);
        refreshButton.ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
        refreshButton.FlatAppearance.BorderColor = ColorTranslator.FromHtml(cfg.ButtonBorderColor);
        refreshButton.FlatAppearance.BorderSize = 1;
        BackColor = ColorTranslator.FromHtml(cfg.WindowBackground);
        ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
        report.BackColor = ColorTranslator.FromHtml(cfg.TextBackground);
        report.ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
        Controls.Add(report);
        Controls.Add(top);

        refreshButton.Click += async (_, _) => await RefreshReport();
        timer.Tick += async (_, _) => await RefreshReport();
        Shown += async (_, _) =>
        {
            await RefreshReport();
            timer.Start();
        };
        FormClosed += (_, _) =>
        {
            timer.Stop();
            timer.Dispose();
            http.Dispose();
        };
    }

    private async Task RefreshReport()
    {
        if (refreshing || IsDisposed) return;
        refreshing = true;
        refreshButton.Enabled = false;
        try
        {
            var lines = new List<string>
            {
                "МОНИТОРИНГ ИИ",
                "Время проверки: " + DateTime.Now.ToString("dd.MM.yyyy HH:mm:ss"),
                new string('=', 76),
                "",
                "OLLAMA"
            };

            try
            {
                using var response = await http.GetAsync(ollamaUrl + "/api/version");
                response.EnsureSuccessStatusCode();
                using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                var version = json.RootElement.TryGetProperty("version", out var v) ? v.GetString() : "?";
                lines.Add("API: доступен; версия " + version);

                using var psResponse = await http.GetAsync(ollamaUrl + "/api/ps");
                psResponse.EnsureSuccessStatusCode();
                using var psJson = JsonDocument.Parse(await psResponse.Content.ReadAsStringAsync());
                var active = psJson.RootElement.GetProperty("models").EnumerateArray().ToList();
                lines.Add("Моделей в памяти: " + active.Count);
                foreach (var model in active)
                {
                    var name = model.TryGetProperty("name", out var n) ? n.GetString() : "?";
                    var size = model.TryGetProperty("size", out var s) && s.TryGetInt64(out var bytes)
                        ? " · " + FormatBytes(bytes) : "";
                    var until = model.TryGetProperty("expires_at", out var e) ? e.ToString() : "";
                    lines.Add("  • " + name + size + (string.IsNullOrWhiteSpace(until) ? "" : " · до " + until));
                }
                if (active.Count == 0) lines.Add("  Сейчас загруженных моделей нет.");
            }
            catch (Exception ex)
            {
                lines.Add("API: недоступен — " + ex.Message);
            }

            lines.Add("");
            lines.Add("OPENCODE — АКТИВНОСТЬ АГЕНТА");
            var cliPath = FindOpenCodeCli();
            if (cliPath is null)
            {
                lines.Add("Сервис OpenCode CLI не найден (процесс opencode-cli.exe отсутствует).");
            }
            else
            {
                try
                {
                    using var infoJson = JsonDocument.Parse(await RunOpenCodeApiAsync(cliPath, "/api/info"));
                    var info = infoJson.RootElement;
                    lines.Add("API: доступен · версия " + (GetString(info, "version") ?? "?"));
                    lines.Add("Сервис PID: " + (GetString(info, "pid") ?? "?"));

                    using var activeJson = JsonDocument.Parse(await RunOpenCodeApiAsync(cliPath, "/api/session/active"));
                    var activeData = activeJson.RootElement.TryGetProperty("data", out var ad) ? ad : default;
                    var activeIds = new HashSet<string>(StringComparer.Ordinal);
                    if (activeData.ValueKind == JsonValueKind.Object)
                    {
                        foreach (var item in activeData.EnumerateObject())
                        {
                            var state = GetString(item.Value, "type") ?? item.Value.ToString();
                            activeIds.Add(item.Name);
                            lines.Add("АГЕНТ: " + (state == "running" ? "РАБОТАЕТ" : state) + " · сессия " + item.Name);
                        }
                    }
                    if (activeIds.Count == 0) lines.Add("АГЕНТ: нет активных сессий.");

                    using var sessionsJson = JsonDocument.Parse(await RunOpenCodeApiAsync(cliPath, "/api/session"));
                    var sessionsData = sessionsJson.RootElement.TryGetProperty("data", out var sd) ? sd : default;
                    var sessions = sessionsData.ValueKind == JsonValueKind.Array
                        ? sessionsData.EnumerateArray().ToList()
                        : new List<JsonElement>();
                    lines.Add("Сессий в истории: " + sessions.Count);
                    foreach (var session in sessions.Take(20))
                    {
                        var id = GetString(session, "id") ?? "?";
                        var title = GetString(session, "title") ?? "(без названия)";
                        var model = session.TryGetProperty("model", out var modelData)
                            ? GetString(modelData, "id") ?? "?" : "?";
                        var agent = GetString(session, "agent") ?? "?";
                        var outcome = GetString(session, "outcome") ?? "неизвестно";
                        var state = activeIds.Contains(id) ? "РАБОТАЕТ" : outcome;
                        var updated = GetTimestamp(session, "time", "updated");
                        var tokenText = "";
                        if (session.TryGetProperty("tokens", out var tokens))
                        {
                            var input = GetString(tokens, "input") ?? "?";
                            var output = GetString(tokens, "output") ?? "?";
                            tokenText = " · токены вход/выход: " + input + "/" + output;
                        }
                        lines.Add("  • [" + state + "] " + title);
                        lines.Add("      Модель: " + model + " · агент: " + agent + tokenText);
                        lines.Add("      ID: " + id + (updated.Length == 0 ? "" : " · обновлена: " + updated));
                    }
                    if (sessions.Count > 20) lines.Add("  … показаны последние 20 сессий.");
                }
                catch (Exception ex)
                {
                    lines.Add("Не удалось получить данные OpenCode: " + ex.Message);
                }
            }

            lines.Add("");
            lines.Add("ПРОЦЕССЫ WINDOWS");
            AppendProcesses(lines, "ollama", "Ollama");
            AppendProcesses(lines, "ollama_llama_server", "Сервер вычислений Ollama");
            AppendProcesses(lines, "opencode", "OpenCode");
            AppendProcesses(lines, "opencode-cli", "Сервис OpenCode CLI");

            lines.Add("");
            lines.Add("ПОЯСНЕНИЯ И ОГРАНИЧЕНИЯ");
            lines.Add("• /api/session/active показывает активные сессии OpenCode; running означает, что агент занят.");
            lines.Add("• /api/session показывает модель, агента, результат, время обновления и счётчики токенов.");
            lines.Add("• Ollama /api/ps показывает загруженные модели, но не доказывает, что модель прямо сейчас генерирует.");
            lines.Add("• Состояние running само по себе ещё не доказывает зависание или генерацию токенов.");


            report.Text = string.Join(Environment.NewLine, lines);
            report.SelectionStart = 0;
            report.SelectionLength = 0;
        }
        catch (Exception ex)
        {
            report.Text = "Ошибка мониторинга: " + ex.Message;
        }
        finally
        {
            refreshing = false;
            if (!IsDisposed) refreshButton.Enabled = true;
        }
    }

    private static string? FindOpenCodeCli()
    {
        Process[] processes;
        try { processes = Process.GetProcessesByName("opencode-cli"); }
        catch { return null; }

        foreach (var process in processes)
        {
            try
            {
                var path = process.MainModule?.FileName;
                if (!string.IsNullOrWhiteSpace(path) && File.Exists(path)) return path;
            }
            catch { }
            finally { process.Dispose(); }
        }
        return null;
    }

    private static async Task<string> RunOpenCodeApiAsync(string executable, string apiPath)
    {
        using var process = new Process();
        process.StartInfo = new ProcessStartInfo
        {
            FileName = executable,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        process.StartInfo.ArgumentList.Add("api");
        process.StartInfo.ArgumentList.Add("get");
        process.StartInfo.ArgumentList.Add(apiPath);

        if (!process.Start()) throw new InvalidOperationException("Не удалось запустить OpenCode CLI.");

        var outputTask = process.StandardOutput.ReadToEndAsync();
        var errorTask = process.StandardError.ReadToEndAsync();
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        try
        {
            await process.WaitForExitAsync(timeout.Token);
        }
        catch (OperationCanceledException)
        {
            try { process.Kill(true); } catch { }
            throw new TimeoutException("OpenCode CLI не ответил за 5 секунд.");
        }

        var output = await outputTask;
        var error = await errorTask;
        if (process.ExitCode != 0)
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(error) ? output.Trim() : error.Trim());
        if (string.IsNullOrWhiteSpace(output))
            throw new InvalidOperationException("OpenCode CLI вернул пустой ответ.");
        return output;
    }

    private static string? GetString(JsonElement element, string property)
    {
        if (element.ValueKind != JsonValueKind.Object ||
            !element.TryGetProperty(property, out var value)) return null;
        return value.ValueKind == JsonValueKind.String ? value.GetString() : value.ToString();
    }

    private static string GetTimestamp(JsonElement session, string parentName, string childName)
    {
        if (!session.TryGetProperty(parentName, out var parent) ||
            !parent.TryGetProperty(childName, out var value)) return "";
        try
        {
            if (value.ValueKind == JsonValueKind.Number && value.TryGetInt64(out var ms))
                return DateTimeOffset.FromUnixTimeMilliseconds(ms).ToLocalTime().ToString("dd.MM HH:mm:ss");
            if (value.ValueKind == JsonValueKind.String && DateTimeOffset.TryParse(value.GetString(), out var date))
                return date.ToLocalTime().ToString("dd.MM HH:mm:ss");
        }
        catch { }
        return "";
    }

    private void AppendProcesses(List<string> lines, string processName, string label)
    {
        Process[] processes;
        try { processes = Process.GetProcessesByName(processName); }
        catch (Exception ex)
        {
            lines.Add(label + ": ошибка чтения — " + ex.Message);
            return;
        }

        if (processes.Length == 0)
        {
            lines.Add(label + ": процесс не найден");
            return;
        }

        foreach (var process in processes)
        {
            try
            {
                process.Refresh();
                var memory = FormatBytes(process.WorkingSet64);
                var now = DateTime.UtcNow;
                var cpu = process.TotalProcessorTime;
                var cpuText = "сбор данных…";
                if (cpuSamples.TryGetValue(process.Id, out var previous))
                {
                    var elapsed = (now - previous.Sample).TotalMilliseconds;
                    if (elapsed > 0)
                    {
                        var percent = (cpu - previous.Cpu).TotalMilliseconds /
                                      (elapsed * Environment.ProcessorCount) * 100.0;
                        cpuText = Math.Max(0, percent).ToString("F1") + "%";
                    }
                }
                cpuSamples[process.Id] = (cpu, now);
                lines.Add(label + " PID " + process.Id + ": CPU " + cpuText + " · RAM " + memory);
            }
            catch (Exception ex)
            {
                lines.Add(label + ": не удалось прочитать показатели — " + ex.Message);
            }
            finally
            {
                process.Dispose();
            }
        }
    }

    private static string FormatBytes(long bytes)
    {
        string[] units = { "Б", "КБ", "МБ", "ГБ", "ТБ" };
        double value = bytes;
        var unit = 0;
        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }
        return value.ToString("F1") + " " + units[unit];
    }
}
