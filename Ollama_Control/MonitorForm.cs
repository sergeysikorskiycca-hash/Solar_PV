using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net.Http;
using System.Text.Json;

namespace OllamaControl;

internal sealed class MonitorForm : Form
{
    private readonly string ollamaUrl;
    private readonly AppSettings cfg;
    private readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(4) };
    private readonly HttpClient eventHttp = new() { Timeout = Timeout.InfiniteTimeSpan };
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
    private readonly ConcurrentQueue<string> openCodeEvents = new();
    private CancellationTokenSource? eventCts;
    private string? eventServerUrl;
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
            eventCts?.Cancel();
            eventCts?.Dispose();
            http.Dispose();
            eventHttp.Dispose();
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
            lines.Add("OPENCODE — API И СЕССИИ");
            var server = await FindOpenCodeServerAsync();
            if (server is null)
            {
                StopEventStream();
                lines.Add("Локальный API-сервер OpenCode не обнаружен.");
                lines.Add("Проверены TCP-порты процессов OpenCode и стандартный адрес localhost:4096.");
                lines.Add("Процесс OpenCode может работать без запущенного режима сервера.");
            }
            else
            {
                lines.Add("API: доступен · " + server.Value.Url);
                lines.Add("Версия: " + server.Value.Version);
                StartEventStream(server.Value.Url);

                try
                {
                    using var sessionsResponse = await http.GetAsync(server.Value.Url + "/session");
                    sessionsResponse.EnsureSuccessStatusCode();
                    using var sessionsJson = JsonDocument.Parse(await sessionsResponse.Content.ReadAsStringAsync());
                    using var statusResponse = await http.GetAsync(server.Value.Url + "/session/status");
                    statusResponse.EnsureSuccessStatusCode();
                    using var statusJson = JsonDocument.Parse(await statusResponse.Content.ReadAsStringAsync());

                    var sessions = sessionsJson.RootElement.ValueKind == JsonValueKind.Array
                        ? sessionsJson.RootElement.EnumerateArray().ToList()
                        : new List<JsonElement>();
                    lines.Add("Сессий в списке: " + sessions.Count);
                    if (sessions.Count == 0) lines.Add("  Сессий нет.");
                    foreach (var session in sessions.Take(30))
                    {
                        var id = GetString(session, "id") ?? GetString(session, "sessionID") ?? "?";
                        var title = GetString(session, "title") ?? "(без названия)";
                        var status = GetSessionStatus(statusJson.RootElement, id);
                        var updated = GetTimestamp(session, "time", "updated");
                        lines.Add("  • [" + status + "] " + title);
                        lines.Add("      ID: " + id + (updated.Length == 0 ? "" : " · обновлена: " + updated));
                    }
                    if (sessions.Count > 30) lines.Add("  … показаны первые 30 сессий.");
                }
                catch (Exception ex)
                {
                    lines.Add("Не удалось получить сессии: " + ex.Message);
                }
            }

            lines.Add("");
            lines.Add("СОБЫТИЯ OPENCODE (последние 20)");
            var recentEvents = openCodeEvents.ToArray();
            if (recentEvents.Length == 0)
                lines.Add(eventServerUrl is null
                    ? "Поток событий не подключён."
                    : "Ожидание событий. События появятся при активности сессий.");
            else
                foreach (var item in recentEvents.Reverse()) lines.Add("  " + item);

            lines.Add("");
            lines.Add("ПРОЦЕССЫ WINDOWS");
            AppendProcesses(lines, "ollama", "Ollama");
            AppendProcesses(lines, "ollama_llama_server", "Сервер вычислений Ollama");
            AppendProcesses(lines, "opencode", "OpenCode");

            lines.Add("");
            lines.Add("ПОЯСНЕНИЯ И ОГРАНИЧЕНИЯ");
            lines.Add("• Состояние busy/idle берётся из API OpenCode, а не вычисляется по загрузке CPU.");
            lines.Add("• События показывают начало/завершение шагов, поток текста и работу инструментов, если их публикует сервер.");
            lines.Add("• События начинают записываться после подключения мониторинга; это не архив прошлых действий.");
            lines.Add("• Ollama /api/ps показывает загруженные модели, но не предоставляет список чужих активных запросов.");
            lines.Add("• Скорость токенов внешнего запроса Ollama нельзя достоверно получить из одного списка процессов.");
            lines.Add("• Низкая нагрузка CPU сама по себе не означает зависание.");

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

    private async Task<(string Url, string Version)?> FindOpenCodeServerAsync()
    {
        var candidates = new HashSet<int>();
        var processIds = new HashSet<int>();
        foreach (var process in Process.GetProcesses())
        {
            try
            {
                if (process.ProcessName.Contains("opencode", StringComparison.OrdinalIgnoreCase))
                    processIds.Add(process.Id);
            }
            catch { }
            finally { process.Dispose(); }
        }

        if (processIds.Count > 0)
        {
            try
            {
                using var netstat = Process.Start(new ProcessStartInfo
                {
                    FileName = "netstat",
                    Arguments = "-ano -p tcp",
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    CreateNoWindow = true
                });
                if (netstat is not null)
                {
                    var output = await netstat.StandardOutput.ReadToEndAsync();
                    await netstat.WaitForExitAsync();
                    foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries))
                    {
                        var parts = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                        if (parts.Length < 5 || !parts[^2].Equals("LISTENING", StringComparison.OrdinalIgnoreCase))
                            continue;
                        if (!int.TryParse(parts[^1], out var pid) || !processIds.Contains(pid))
                            continue;
                        var endpoint = parts[1];
                        var colon = endpoint.LastIndexOf(':');
                        if (colon >= 0 && int.TryParse(endpoint[(colon + 1)..], out var port))
                            candidates.Add(port);
                    }
                }
            }
            catch { }
        }

        candidates.Add(4096);
        foreach (var port in candidates.OrderBy(p => p == 4096 ? 0 : 1))
        {
            var url = "http://127.0.0.1:" + port;
            try
            {
                using var response = await http.GetAsync(url + "/global/health");
                if (!response.IsSuccessStatusCode) continue;
                using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync());
                if (!json.RootElement.TryGetProperty("healthy", out var healthy) || healthy.ValueKind != JsonValueKind.True)
                    continue;
                var version = GetString(json.RootElement, "version") ?? "?";
                return (url, version);
            }
            catch { }
        }
        return null;
    }

    private void StartEventStream(string baseUrl)
    {
        if (string.Equals(eventServerUrl, baseUrl, StringComparison.OrdinalIgnoreCase) &&
            eventCts is { IsCancellationRequested: false }) return;

        StopEventStream();
        eventServerUrl = baseUrl;
        eventCts = new CancellationTokenSource();
        var token = eventCts.Token;
        _ = Task.Run(() => ReadEventStreamAsync(baseUrl, token), token);
    }

    private void StopEventStream()
    {
        eventCts?.Cancel();
        eventCts?.Dispose();
        eventCts = null;
        eventServerUrl = null;
    }

    private async Task ReadEventStreamAsync(string baseUrl, CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            try
            {
                using var request = new HttpRequestMessage(HttpMethod.Get, baseUrl + "/event");
                using var response = await eventHttp.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, token);
                response.EnsureSuccessStatusCode();
                await using var stream = await response.Content.ReadAsStreamAsync(token);
                using var reader = new StreamReader(stream);
                string? line;
                var data = new List<string>();
                while (!token.IsCancellationRequested && (line = await reader.ReadLineAsync(token)) is not null)
                {
                    if (line.StartsWith("data:", StringComparison.Ordinal))
                        data.Add(line[5..].Trim());
                    else if (line.Length == 0 && data.Count > 0)
                    {
                        AddEvent(string.Join("", data));
                        data.Clear();
                    }
                }
            }
            catch (OperationCanceledException) { break; }
            catch (Exception ex)
            {
                AddEvent("Поток событий: переподключение (" + ex.Message + ")");
            }

            try { await Task.Delay(2000, token); }
            catch (OperationCanceledException) { break; }
        }
    }

    private void AddEvent(string jsonText)
    {
        try
        {
            using var json = JsonDocument.Parse(jsonText);
            var root = json.RootElement;
            var payload = root.TryGetProperty("payload", out var p) ? p : root;
            var type = GetString(payload, "type") ?? GetString(root, "type") ?? "событие";
            var properties = payload.TryGetProperty("properties", out var props) ? props : payload;
            var sessionId = GetString(properties, "sessionID") ?? GetString(properties, "sessionId");
            var tool = GetString(properties, "tool") ?? GetString(properties, "name");
            var detail = type;
            if (!string.IsNullOrWhiteSpace(sessionId)) detail += " · сессия " + sessionId;
            if (!string.IsNullOrWhiteSpace(tool)) detail += " · " + tool;
            EnqueueEvent(DateTime.Now.ToString("HH:mm:ss") + " " + detail);
        }
        catch
        {
            EnqueueEvent(DateTime.Now.ToString("HH:mm:ss") + " Получено событие OpenCode.");
        }
    }

    private void EnqueueEvent(string value)
    {
        openCodeEvents.Enqueue(value);
        while (openCodeEvents.Count > 20) openCodeEvents.TryDequeue(out _);
    }

    private static string GetSessionStatus(JsonElement statuses, string id)
    {
        if (statuses.ValueKind == JsonValueKind.Object &&
            statuses.TryGetProperty(id, out var status))
        {
            if (status.ValueKind == JsonValueKind.String) return status.GetString() ?? "неизвестно";
            return GetString(status, "type") ?? GetString(status, "status") ?? status.ToString();
        }
        return "нет статуса";
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
