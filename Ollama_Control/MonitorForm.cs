using System.Diagnostics;
using System.Net.Http;
using System.Text.Json;

namespace OllamaControl;

internal sealed class MonitorForm : Form
{
    private readonly string ollamaUrl;
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

    public MonitorForm(string apiUrl)
    {
        ollamaUrl = apiUrl.TrimEnd('/');
        Text = "Мониторинг ИИ";
        Width = 850;
        Height = 620;
        MinimumSize = new Size(650, 420);
        StartPosition = FormStartPosition.CenterParent;

        var top = new FlowLayoutPanel { Dock = DockStyle.Top, AutoSize = true, Padding = new Padding(8) };
        top.Controls.Add(refreshButton);
        top.Controls.Add(new Label
        {
            Text = "Автообновление каждые 3 секунды",
            AutoSize = true,
            Padding = new Padding(8, 7, 0, 0)
        });
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
        refreshButton.Enabled = false;
        try
        {
            var lines = new List<string>
            {
                "МОНИТОРИНГ ИИ",
                "Время проверки: " + DateTime.Now.ToString("dd.MM.yyyy HH:mm:ss"),
                new string('=', 68),
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
            lines.Add("ПРОЦЕССЫ WINDOWS");
            AppendProcesses(lines, "ollama", "Ollama");
            AppendProcesses(lines, "ollama_llama_server", "Сервер вычислений Ollama");
            AppendProcesses(lines, "opencode", "OpenCode");

            lines.Add("");
            lines.Add("КАК ЧИТАТЬ ПОКАЗАТЕЛИ");
            lines.Add("• CPU и RAM относятся к процессам Windows, а не к отдельной модели.");
            lines.Add("• Наличие процесса Ollama или загруженной модели не доказывает, что сейчас идёт генерация.");
            lines.Add("• Для OpenCode здесь видны только процессы. Состояние конкретной сессии, поток токенов и работа инструментов");
            lines.Add("  требуют подключения к API-серверу OpenCode и его потоку событий; это не заменяется показателями CPU/RAM.");
            lines.Add("• Нулевая или низкая нагрузка сама по себе не означает зависание.");
            lines.Add("");
            lines.Add("Остановка генерации из этого окна не предлагается: универсального безопасного способа");
            lines.Add("прервать произвольный запрос, запущенный другим приложением через Ollama, нет.");

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
            refreshButton.Enabled = true;
        }
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
