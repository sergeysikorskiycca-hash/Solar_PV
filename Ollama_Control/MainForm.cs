using System.Diagnostics;
using System.Drawing;

namespace OllamaControl;

internal sealed class MainForm : Form
{
    private readonly AppSettings cfg = AppSettings.Load();
    private readonly OllamaClient api = new();
    private readonly MenuStrip menu = new();
    private readonly TextBox address = new() { Width = 220 };
    private readonly TextBox keep = new() { Width = 70 };
    private readonly ComboBox models = new() { Width = 280, DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox selectedModel = CopyableTextBox(280, 25);
    private readonly TextBox installedModelsText = CopyableTextBox(420, 70, true);
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

        var contextMenu = new ContextMenuStrip();
        contextMenu.Items.Add("Копировать", null, (_, _) => box.Copy());
        contextMenu.Items.Add("Выделить всё", null, (_, _) => { box.Focus(); box.SelectAll(); });
        box.ContextMenuStrip = contextMenu;
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
            RowCount = 5
        };
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        Controls.Add(root);

        var settings = new ToolStripMenuItem("Настройки");
        var colors = new ToolStripMenuItem("Цвета");
        colors.DropDownItems.Add("Цвет фона", null, (_, _) => ChooseBackground());
        colors.DropDownItems.Add("Цвет текста", null, (_, _) => ChooseTextColor());
        settings.DropDownItems.Add(colors);
        menu.Items.Add(settings);
        menu.Dock = DockStyle.Fill;
        root.Controls.Add(menu, 0, 0);

        var top = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
        top.Controls.Add(CopyableTextBox(105, 25).Also(x => x.Text = "Адрес Ollama:"));
        top.Controls.Add(address);
        top.Controls.Add(Btn("Проверить", RefreshAll));
        top.Controls.Add(status);
        root.Controls.Add(top, 0, 1);

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
        actions.Controls.Add(Btn("Проверить обновления", () => UpdateManager.CheckAsync(this)));
        root.Controls.Add(actions, 0, 2);

        var mid = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
        mid.Controls.Add(CopyableTextBox(125, 25).Also(x => x.Text = "Выбранная модель:"));
        mid.Controls.Add(selectedModel);
        mid.Controls.Add(CopyableTextBox(125, 25).Also(x => x.Text = "Все модели:"));
        mid.Controls.Add(installedModelsText);
        mid.Controls.Add(CopyableTextBox(125, 25).Also(x => x.Text = "Модели в памяти:"));
        mid.Controls.Add(running);
        root.Controls.Add(mid, 0, 3);
        log.Dock = DockStyle.Fill;
        root.Controls.Add(log, 0, 4);
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
        menu.BackColor = ColorTranslator.FromHtml(cfg.WindowBackground);
        menu.ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
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

    private void ChooseBackground()
    {
        using var dialog = new ColorDialog
        {
            Color = ColorTranslator.FromHtml(cfg.WindowBackground),
            FullOpen = true
        };

        if (dialog.ShowDialog(this) != DialogResult.OK) return;

        var color = ToHtmlColor(dialog.Color);
        cfg.WindowBackground = color;
        cfg.PanelBackground = color;
        cfg.Save();
        Theme();
    }

    private void ChooseTextColor()
    {
        using var dialog = new ColorDialog
        {
            Color = ColorTranslator.FromHtml(cfg.TextPrimary),
            FullOpen = true
        };

        if (dialog.ShowDialog(this) != DialogResult.OK) return;

        cfg.TextPrimary = ToHtmlColor(dialog.Color);
        cfg.Save();
        Theme();
    }

    private static string ToHtmlColor(Color color) =>
        "#" + color.R.ToString("X2") + color.G.ToString("X2") + color.B.ToString("X2");

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
        installedModelsText.Text = string.Join(Environment.NewLine, installed);
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
