using System.Diagnostics;
using System.Drawing;

namespace OllamaControl;

internal sealed class MainForm : Form
{
    private readonly AppSettings cfg = AppSettings.Load();
    private readonly OllamaClient api = new();
    private readonly TextBox address = new() { Width = 220 };
    private readonly TextBox keep = new() { Width = 70 };
    private readonly ComboBox models = new() { Width = 280, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox selectedModel = CopyableTextBox(280, 25);
    private readonly TextBox running = CopyableTextBox(420, 90, true);
    private readonly TextBox log = CopyableTextBox(0, 0, true);
    private readonly TextBox status = CopyableTextBox(180, 25);

    public MainForm()
    {
        Text = "Ollama Control";
        Width = 820;
        Height = 590;
        MinimumSize = new Size(700, 480);
        StartPosition = FormStartPosition.CenterScreen;

        address.Text = cfg.ApiUrl;
        keep.Text = cfg.KeepAlive;
        status.Text = "Не подключено";

        BuildUi();
        Theme();
        models.SelectedIndexChanged += (_, _) => selectedModel.Text = models.SelectedItem?.ToString() ?? "";
        Shown += async (_, _) => await RefreshAll();
    }

    private static TextBox CopyableTextBox(int width, int height, bool multiline = false)
    {
        var box = new TextBox
        {
            Width = width,
            Height = height,
            Multiline = multiline,
            ReadOnly = true,
            BorderStyle = BorderStyle.FixedSingle,
            ScrollBars = multiline ? ScrollBars.Vertical : ScrollBars.None,
            WordWrap = false,
            TabStop = true
        };

        var menu = new ContextMenuStrip();
        menu.Items.Add("Копировать", null, (_, _) => box.Copy());
        menu.Items.Add("Выделить всё", null, (_, _) => { box.Focus(); box.SelectAll(); });
        box.ContextMenuStrip = menu;
        box.KeyDown += (_, e) =>
        {
            if (e.Control && e.KeyCode == Keys.A) { box.SelectAll(); e.SuppressKeyPress = true; }
            if (e.Control && e.KeyCode == Keys.C) { box.Copy(); e.SuppressKeyPress = true; }
        };
        return box;
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(12),
            ColumnCount = 1,
            RowCount = 4
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        Controls.Add(root);

        var top = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
        top.Controls.Add(CopyableTextBox(105, 25).Also(x => x.Text = "Адрес Ollama:"));
        top.Controls.Add(address);
        top.Controls.Add(Btn("Проверить", RefreshAll));
        top.Controls.Add(CopyableTextBox(180, 25).Also(x => x.Text = status.Text));
        var statusField = top.Controls[top.Controls.Count - 1] as TextBox;
        statusField!.DataBindings.Add("Text", status, "Text");
        root.Controls.Add(top);

        var actions = new FlowLayoutPanel
        {
            AutoSize = true,
            Dock = DockStyle.Fill,
            Padding = new Padding(0, 10, 0, 8)
        };
        actions.Controls.Add(CopyableTextBox(60, 25).Also(x => x.Text = "Модель:"));
        actions.Controls.Add(models);
        actions.Controls.Add(Btn("Обновить списки", RefreshAll));
        actions.Controls.Add(CopyableTextBox(55, 25).Also(x => x.Text = "Время:"));
        actions.Controls.Add(keep);
        actions.Controls.Add(Btn("Загрузить", LoadSelected));
        actions.Controls.Add(Btn("Выгрузить", StopSelected));
        actions.Controls.Add(Btn("Выгрузить все", StopAll));
        actions.Controls.Add(Btn("OpenCode", LaunchOpenCode));
        actions.Controls.Add(Btn("Цвета", ChooseColors));
        actions.Controls.Add(Btn("Проверить обновления", () => UpdateManager.CheckAsync(this)));
        root.Controls.Add(actions);

        var mid = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
        mid.Controls.Add(CopyableTextBox(125, 25).Also(x => x.Text = "Выбранная модель:"));
        mid.Controls.Add(selectedModel);
        mid.Controls.Add(CopyableTextBox(125, 25).Also(x => x.Text = "Модели в памяти:"));
        mid.Controls.Add(running);
        root.Controls.Add(mid);
        root.Controls.Add(log);
    }

    private Button Btn(string caption, Func<Task> action)
    {
        var button = new Button { Text = caption, AutoSize = true, Padding = new Padding(7, 5, 7, 5) };
        button.Click += async (_, _) =>
        {
            try { await action(); }
            catch (Exception e) { Write("Ошибка: " + e.Message); }
        };
        return button;
    }

    private void Theme()
    {
        BackColor = ColorTranslator.FromHtml(cfg.WindowBackground);
        ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
        ApplyTheme(this);
        log.BackColor = ColorTranslator.FromHtml(cfg.LogBackground);
        log.ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
        address.BackColor = keep.BackColor = ColorTranslator.FromHtml(cfg.InputBackground);
        address.ForeColor = keep.ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
    }

    private void ApplyTheme(Control parent)
    {
        foreach (Control control in parent.Controls)
        {
            control.ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
            control.BackColor = ColorTranslator.FromHtml(
                control is Button ? cfg.ButtonBackground :
                control is TextBox || control is ComboBox ? cfg.InputBackground :
                cfg.PanelBackground);
            ApplyTheme(control);
        }
    }

    private async Task RefreshAll()
    {
        api.BaseUrl = address.Text.Trim();
        cfg.ApiUrl = api.BaseUrl;
        cfg.KeepAlive = keep.Text.Trim();
        cfg.Save();

        var version = await api.VersionAsync();
        var installed = await api.ModelsAsync("/api/tags");
        var active = await api.ModelsAsync("/api/ps");

        models.Items.Clear();
        models.Items.AddRange(installed.Cast<object>().ToArray());
        if (models.Items.Count > 0) models.SelectedIndex = 0;
        else selectedModel.Clear();

        status.Text = "Ollama " + version + " · подключено";
        running.Text = string.Join(Environment.NewLine, active);
        Write("Моделей: " + installed.Count + "; в памяти: " + active.Count);
    }

    private string Selected => models.SelectedItem?.ToString() ?? "";

    private async Task LoadSelected()
    {
        if (Selected.Length == 0) return;
        await api.LoadAsync(Selected, keep.Text.Trim());
        Write("Загрузка: " + Selected);
        await RefreshRunning();
    }

    private async Task StopSelected()
    {
        if (Selected.Length == 0) return;
        await api.StopAsync(Selected);
        Write("Выгрузка: " + Selected);
        await RefreshRunning();
    }

    private async Task StopAll()
    {
        if (MessageBox.Show("Выгрузить все модели из памяти?", "Подтверждение",
            MessageBoxButtons.YesNo) != DialogResult.Yes) return;

        foreach (var model in running.Lines.Where(x => !string.IsNullOrWhiteSpace(x)).ToArray())
            await api.StopAsync(model);

        Write("Отправлены команды выгрузки.");
        await RefreshRunning();
    }

    private async Task RefreshRunning()
    {
        var active = await api.ModelsAsync("/api/ps");
        running.Text = string.Join(Environment.NewLine, active);
    }

    private Task LaunchOpenCode()
    {
        Process.Start(new ProcessStartInfo("opencode") { UseShellExecute = true });
        Write("Запуск OpenCode.");
        return Task.CompletedTask;
    }

    private Task ChooseColors()
    {
        var choices = new (string, Func<string>, Action<string>)[]
        {
            ("Текст", () => cfg.TextPrimary, v => cfg.TextPrimary = v),
            ("Вторичный текст", () => cfg.TextSecondary, v => cfg.TextSecondary = v),
            ("Окно", () => cfg.WindowBackground, v => cfg.WindowBackground = v),
            ("Панели", () => cfg.PanelBackground, v => cfg.PanelBackground = v),
            ("Поля", () => cfg.InputBackground, v => cfg.InputBackground = v),
            ("Кнопки", () => cfg.ButtonBackground, v => cfg.ButtonBackground = v),
            ("Журнал", () => cfg.LogBackground, v => cfg.LogBackground = v)
        };

        foreach (var choice in choices)
        {
            using var dialog = new ColorDialog { Color = ColorTranslator.FromHtml(choice.Item2()) };
            if (dialog.ShowDialog(this) == DialogResult.OK)
                choice.Item3("#" + dialog.Color.R.ToString("X2") +
                    dialog.Color.G.ToString("X2") + dialog.Color.B.ToString("X2"));
        }

        cfg.Save();
        Theme();
        return Task.CompletedTask;
    }

    private void Write(string message) =>
        log.AppendText("[" + DateTime.Now.ToString("HH:mm:ss") + "] " + message + Environment.NewLine);
}

internal static class ControlExtensions
{
    public static T Also<T>(this T control, Action<T> configure) where T : Control
    {
        configure(control);
        return control;
    }
}
