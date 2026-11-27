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
    private readonly ComboBox models = new() { Width = 280, DropDownStyle = ComboBoxStyle.DropDownList, DrawMode = DrawMode.OwnerDrawFixed };
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
        models.DrawItem += DrawModelItem;
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
        settings.DropDownItems.Add("Цвета интерфейса...", null, (_, _) => EditColors());
        menu.Items.Add(settings);
        var monitor = new ToolStripMenuItem("Мониторинг");
        monitor.DropDownItems.Add("Открыть мониторинг ИИ", null, (_, _) => new MonitorForm(address.Text.Trim(), cfg).Show(this));
        menu.Items.Add(monitor);
        var system = new ToolStripMenuItem("Система");
        system.DropDownItems.Add("Проверить обновления программы", null, async (_, _) => await UpdateManager.CheckAsync(this));
        menu.Items.Add(system);
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
        root.Controls.Add(actions, 0, 2);

        var mid = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill };
        mid.Controls.Add(CopyableTextBox(125, 25).Also(x => x.Text = "Выбранная модель:"));
        mid.Controls.Add(selectedModel);
        mid.Controls.Add(CopyableTextBox(125, 25).Also(x => x.Text = "Все модели:"));
        mid.Controls.Add(installedModelsText);
        mid.Controls.Add(CopyableTextBox(125, 25).Also(x => x.Text = "Модели в памяти:"));
        mid.Controls.Add(running);
        mid.Controls.Add(Btn("Обновить модели в памяти", RefreshRunning));
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
        log.BackColor = ColorTranslator.FromHtml(cfg.TextBackground);
        log.ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
        address.BackColor = keep.BackColor = ColorTranslator.FromHtml(cfg.InputBackground);
        selectedModel.BackColor = ColorTranslator.FromHtml(cfg.SelectedModelBackground);
        address.ForeColor = keep.ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
    }

    private void DrawModelItem(object? sender, DrawItemEventArgs e)\n    {\n        e.DrawBackground();\n        if (e.Index >= 0)\n        {\n            using var background = new SolidBrush(ColorTranslator.FromHtml(cfg.ModelListBackground));\n            using var foreground = new SolidBrush(ColorTranslator.FromHtml(cfg.TextPrimary));\n            e.Graphics.FillRectangle(background, e.Bounds);\n            var value = models.Items[e.Index]?.ToString() ?? "";\n            TextRenderer.DrawText(e.Graphics, value, e.Font, e.Bounds, foreground.Color,\n                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);\n        }\n        e.DrawFocusRectangle();\n    }\n\n    private void ApplyTheme(Control parent)
    {
        foreach (Control control in parent.Controls)
        {
            control.ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary);
            control.BackColor = ColorTranslator.FromHtml(
                control is Button ? cfg.ButtonBackground :
                control is TextBox textBox ? (textBox.ReadOnly ? cfg.TextBackground : cfg.InputBackground) :
                control is ComboBox ? cfg.InputBackground :
                cfg.PanelBackground);
            if (control is Button button)
            {
                button.FlatStyle = FlatStyle.Flat;
                button.FlatAppearance.BorderColor = ColorTranslator.FromHtml(cfg.ButtonBorderColor);
                button.FlatAppearance.BorderSize = 1;
            }
            ApplyTheme(control);
        }
    }

    private void EditColors()
    {
        var dialog = new Form
        {
            Text = "Цвета интерфейса",
            FormBorderStyle = FormBorderStyle.FixedDialog,
            StartPosition = FormStartPosition.CenterParent,
            MinimizeBox = false,
            MaximizeBox = false,
            ShowInTaskbar = false,
            ClientSize = new Size(390, 435),
            BackColor = ColorTranslator.FromHtml(cfg.WindowBackground),
            ForeColor = ColorTranslator.FromHtml(cfg.TextPrimary)
        };

        var colors = new (string Label, Func<string> Get, Action<string> Set)[]
        {
            ("Фон окна", () => cfg.WindowBackground, value => cfg.WindowBackground = value),
            ("Фон панелей", () => cfg.PanelBackground, value => cfg.PanelBackground = value),
            ("Фон полей ввода", () => cfg.InputBackground, value => cfg.InputBackground = value),\n            ("Фон списка моделей", () => cfg.ModelListBackground, value => cfg.ModelListBackground = value),\n            ("Фон выбранной модели", () => cfg.SelectedModelBackground, value => cfg.SelectedModelBackground = value),
            ("Фон кнопок", () => cfg.ButtonBackground, value => cfg.ButtonBackground = value),
            ("Контур кнопок", () => cfg.ButtonBorderColor, value => cfg.ButtonBorderColor = value),
            ("Фон информационных полей", () => cfg.TextBackground, value => cfg.TextBackground = value),
            ("Основной цвет текста", () => cfg.TextPrimary, value => cfg.TextPrimary = value)
        };
        var selectedColors = colors.Select(item => item.Get()).ToArray();

        var layout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(12),
            ColumnCount = 2,
            RowCount = colors.Length + 1,
            BackColor = dialog.BackColor
        };
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 115));

        for (var i = 0; i < colors.Length; i++)
        {
            var index = i;
            var label = new Label
            {
                Text = colors[index].Label,
                AutoSize = true,
                Anchor = AnchorStyles.Left,
                ForeColor = dialog.ForeColor,
                Margin = new Padding(3, 9, 3, 9)
            };
            var colorButton = new Button
            {
                Text = selectedColors[index],
                Width = 105,
                Height = 28,
                BackColor = ColorTranslator.FromHtml(selectedColors[index]),
                ForeColor = dialog.ForeColor,
                FlatStyle = FlatStyle.Flat,
                Margin = new Padding(3, 3, 3, 3)
            };
            colorButton.Click += (_, _) =>
            {
                if (!PickColor(dialog, ColorTranslator.FromHtml(selectedColors[index]), out var pickedColor)) return;
                selectedColors[index] = ToHtmlColor(pickedColor);
                colorButton.Text = selectedColors[index];
                colorButton.BackColor = pickedColor;
            };
            layout.Controls.Add(label);
            layout.Controls.Add(colorButton);
        }

        var buttons = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            FlowDirection = FlowDirection.RightToLeft,
            AutoSize = true,
            WrapContents = false
        };
        var ok = new Button { Text = "Сохранить", DialogResult = DialogResult.OK, AutoSize = true };
        var cancel = new Button { Text = "Отмена", DialogResult = DialogResult.Cancel, AutoSize = true };
        buttons.Controls.Add(ok);
        buttons.Controls.Add(cancel);
        layout.Controls.Add(buttons, 0, colors.Length);
        layout.SetColumnSpan(buttons, 2);
        dialog.Controls.Add(layout);
        dialog.AcceptButton = ok;
        dialog.CancelButton = cancel;

        if (dialog.ShowDialog(this) != DialogResult.OK) return;
        for (var i = 0; i < colors.Length; i++)
            colors[i].Set(selectedColors[i]);
        cfg.Save();
        Theme();
    }

    private bool PickColor(IWin32Window owner, Color initialColor, out Color selectedColor)
    {
        using var picker = new ColorDialog
        {
            Color = initialColor,
            FullOpen = true,
            CustomColors = cfg.CustomColors
        };

        var result = picker.ShowDialog(owner);

        // Сохраняем палитру даже если пользователь закрыл выбор цвета кнопкой «Отмена».
        var updatedPalette = picker.CustomColors ?? Array.Empty<int>();
        if (!cfg.CustomColors.SequenceEqual(updatedPalette))
        {
            cfg.CustomColors = updatedPalette.ToArray();
            cfg.Save();
        }

        selectedColor = picker.Color;
        return result == DialogResult.OK;
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
