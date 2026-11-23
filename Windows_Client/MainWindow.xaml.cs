using System.Globalization;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;

namespace SolarPV.Client;

public partial class MainWindow : Window
{
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(3) };
    private readonly DispatcherTimer _pollTimer = new() { Interval = TimeSpan.FromSeconds(3) };
    private bool _connected;
    private bool _pollInProgress;
    private bool _connectionFailureLogged;
    private static readonly string ConnectionLogPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "Solar_PV", "connection.log");
    private string _baseAddress = "";
    private string[]? _activePalette;

    public MainWindow()
    {
        InitializeComponent();
        UpdateWindowStateButton();
        var version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "—";
        VersionText.Text = $"Solar_PV Desktop {version}";
        AddressBox.Text = ClientSettings.LoadControllerAddress() ?? AddressBox.Text;
        Loaded += (_, _) => ApplyTheme(ClientSettings.LoadTheme() ?? "Digital");
        _pollTimer.Tick += async (_, _) => await PollAsync();
        Closed += (_, _) => WriteConnectionLog("Клиент Solar_PV закрыт.");
        ShowDemo();
    }

    private void TitleBar_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (e.ChangedButton != System.Windows.Input.MouseButton.Left) return;
        if (e.ClickCount == 2) ToggleWindowState();
        else DragMove();
    }

    private void MinimizeWindow_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void MaximizeRestoreWindow_Click(object sender, RoutedEventArgs e) => ToggleWindowState();
    private void CloseWindow_Click(object sender, RoutedEventArgs e) => Close();
    private void MainWindow_StateChanged(object? sender, EventArgs e) => UpdateWindowStateButton();

    private void ToggleWindowState()
    {
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
        UpdateWindowStateButton();
    }

    private void UpdateWindowStateButton()
    {
        if (MaximizeRestoreButton == null) return;
        var maximized = WindowState == WindowState.Maximized;
        MaximizeRestoreButton.Content = maximized ? "❐" : "□";
        MaximizeRestoreButton.ToolTip = maximized ? "Восстановить" : "Развернуть";
    }

    private void Exit_Click(object sender, RoutedEventArgs e) => Close();

    private void About_Click(object sender, RoutedEventArgs e)
    {
        var version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "—";
        MessageBox.Show($"Solar_PV Desktop {version}\nКлиент мониторинга солнечной станции\nWindows 10 / 11",
            "О программе", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private void Theme_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not System.Windows.Controls.MenuItem item || item.Tag is not string theme) return;
        ApplyTheme(theme);
        ClientSettings.SaveTheme(theme);
    }

    private void ApplyTheme(string theme)
    {
        var palette = theme switch
        {
            "Analog" => new[] { "#101B25", "#192D3B", "#EDF7FB", "#A3BBC9", "#5BD0F0", "#EDF7FB", "#426174", "#244253" },
            "Flow" => new[] { "#071711", "#10271B", "#E9F7EC", "#94B69C", "#80E078", "#E9F7EC", "#376047", "#1B3A28" },
            "Blueprint" => new[] { "#071827", "#0C2638", "#DDF6FF", "#83B2C5", "#36D8E8", "#DDF6FF", "#24617C", "#10344A" },
            _ => new[] { "#080D12", "#121D26", "#EDF4F8", "#8EA3B1", "#FFB547", "#EDF4F8", "#354957", "#1B2A35" }
        };
        var map = new Dictionary<string, string>
        {
            ["#FF0B1220"] = palette[0], ["#FF121E30"] = palette[1],
            ["#FFE8EEF7"] = palette[2], ["#FF8FA2BA"] = palette[3],
            ["#FFF5B942"] = palette[4], ["#FF17202D"] = palette[0],
            ["#FF17263A"] = palette[7], ["#FF30445E"] = palette[6],
            ["#FF26384D"] = palette[7], ["#FF405875"] = palette[6],
            ["#FF1A2B3E"] = palette[1]
        };

        // Re-map colors from the currently active theme as well as the original XAML colors.
        // Without this, a second theme switch cannot recognize brushes changed by the first one.
        if (_activePalette is not null)
        {
            int[] paletteSlots = { 0, 1, 2, 3, 4, 6, 7 };
            foreach (var slot in paletteSlots)
            {
                var oldColor = "#FF" + _activePalette[slot].TrimStart('#');
                map[oldColor] = palette[slot];
            }
        }
        Brush ConvertBrush(Brush brush)
        {
            if (brush is SolidColorBrush solid)
            {
                var key = solid.Color.ToString();
                if (map.TryGetValue(key, out var replacement))
                    return new SolidColorBrush((Color)ColorConverter.ConvertFromString(replacement));
            }
            return brush;
        }
        SolidColorBrush PaletteBrush(string value) =>
            new((Color)ColorConverter.ConvertFromString(value));
        Background = PaletteBrush(palette[0]);
        Foreground = PaletteBrush(palette[2]);
        MainMenu.Background = PaletteBrush(palette[7]);
        MainMenu.Foreground = PaletteBrush(palette[2]);
        var menuItemStyle = new Style(typeof(System.Windows.Controls.MenuItem));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush(palette[7])));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush(palette[2])));
        menuItemStyle.Setters.Add(new Setter(
            System.Windows.Controls.MenuItem.HeaderTemplateProperty,
            (DataTemplate)FindResource("MenuItemHeaderTemplate")));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.PaddingProperty, new Thickness(10, 6, 10, 6)));
        var highlighted = new Trigger
        {
            Property = System.Windows.Controls.MenuItem.IsHighlightedProperty,
            Value = true
        };
        highlighted.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush("#DCE6F2")));
        highlighted.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush("#17202D")));
        menuItemStyle.Triggers.Add(highlighted);
        var hovered = new Trigger
        {
            Property = System.Windows.UIElement.IsMouseOverProperty,
            Value = true
        };
        hovered.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush("#DCE6F2")));
        hovered.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush("#17202D")));
        menuItemStyle.Triggers.Add(hovered);
        MainMenu.Resources[typeof(System.Windows.Controls.MenuItem)] = menuItemStyle;
        Application.Current.Resources[typeof(System.Windows.Controls.MenuItem)] = menuItemStyle;
        MainMenu.ItemContainerStyle = menuItemStyle;

        // Explicitly refresh already-created menu containers when the theme changes.
        // ItemContainerStyle alone does not reliably replace styles on existing XAML items.
        void ApplyMenuItemStyle(System.Windows.Controls.MenuItem item)
        {
            item.Style = menuItemStyle;
            foreach (var child in item.Items)
            {
                if (child is System.Windows.Controls.MenuItem childMenuItem)
                    ApplyMenuItemStyle(childMenuItem);
            }
        }
        foreach (var menuEntry in MainMenu.Items)
        {
            if (menuEntry is System.Windows.Controls.MenuItem menuItem)
                ApplyMenuItemStyle(menuItem);
        }

        void Visit(DependencyObject parent)
        {
            for (var i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
            {
                var child = System.Windows.Media.VisualTreeHelper.GetChild(parent, i);
                if (child is System.Windows.Controls.Control control)
                {
                    if (control.Background is SolidColorBrush bg)
                        control.Background = ConvertBrush(bg);
                    if (control.Foreground is SolidColorBrush fg)
                        control.Foreground = ConvertBrush(fg);
                }
                if (child is System.Windows.Controls.TextBlock textBlock && textBlock.Foreground is SolidColorBrush textBrush)
                    textBlock.Foreground = ConvertBrush(textBrush);
                if (child is System.Windows.Controls.Border border && border.Background is SolidColorBrush borderBg)
                    border.Background = ConvertBrush(borderBg);
                Visit(child);
            }
        }
        Visit(this);

        // Four desktop design grammars share the same live telemetry and controls.
        var cornerRadius = theme switch
        {
            "Analog" => new CornerRadius(16),
            "Flow" => new CornerRadius(1),
            "Blueprint" => new CornerRadius(0),
            _ => new CornerRadius(4)
        };
        var cardBorder = PaletteBrush(palette[6]);
        var accent = PaletteBrush(palette[4]);

        void StyleCards(DependencyObject parent)
        {
            for (var i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
            {
                var child = System.Windows.Media.VisualTreeHelper.GetChild(parent, i);
                if (child is System.Windows.Controls.Border border)
                {
                    border.CornerRadius = cornerRadius;
                    border.BorderBrush = theme == "Flow" ? accent : cardBorder;
                    border.BorderThickness = theme == "Flow"
                        ? new Thickness(3, 1, 1, 1)
                        : new Thickness(theme == "Blueprint" ? 1.2 : 1);
                }
                StyleCards(child);
            }
        }
        StyleCards(this);

        if (theme == "Blueprint")
        {
            var gridLines = new GeometryGroup();
            gridLines.Children.Add(new LineGeometry(new Point(0, 0), new Point(32, 0)));
            gridLines.Children.Add(new LineGeometry(new Point(0, 0), new Point(0, 32)));
            var drawing = new DrawingGroup();
            drawing.Children.Add(new GeometryDrawing(PaletteBrush(palette[0]), null,
                new RectangleGeometry(new Rect(0, 0, 32, 32))));
            drawing.Children.Add(new GeometryDrawing(null, new Pen(PaletteBrush(palette[6]), 0.45), gridLines));
            Background = new DrawingBrush(drawing)
            {
                TileMode = TileMode.Tile,
                Viewport = new Rect(0, 0, 32, 32),
                ViewportUnits = BrushMappingMode.Absolute,
                Viewbox = new Rect(0, 0, 32, 32),
                ViewboxUnits = BrushMappingMode.Absolute,
                Stretch = Stretch.None
            };
        }

        var valueFont = theme switch
        {
            "Analog" => new FontFamily("Georgia"),
            "Blueprint" => new FontFamily("Consolas"),
            _ => new FontFamily("Segoe UI")
        };
        TotalPowerText.FontFamily = valueFont;
        TotalEnergyText.FontFamily = valueFont;
        String1Power.FontFamily = valueFont;
        String2Power.FontFamily = valueFont;
        TotalPowerText.FontSize = theme == "Analog" ? 36 : 34;
        TotalEnergyText.FontSize = theme == "Analog" ? 36 : 34;
        String1Power.FontSize = theme == "Analog" ? 31 : 29;
        String2Power.FontSize = theme == "Analog" ? 31 : 29;
        if (theme == "Flow")
        {
            TotalPowerText.Foreground = accent;
            String1Power.Foreground = accent;
            String2Power.Foreground = accent;
        }

        var topMetricsTextColor = palette[2];

        void ApplyTopMetricsTextColor(DependencyObject parent)
        {
            if (parent is System.Windows.Controls.TextBlock textBlock)
                textBlock.Foreground = PaletteBrush(topMetricsTextColor);

            for (var i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
                ApplyTopMetricsTextColor(System.Windows.Media.VisualTreeHelper.GetChild(parent, i));
        }

        ApplyTopMetricsTextColor(TopMetricsGrid);
        if (theme == "Flow")
        {
            TotalPowerText.Foreground = accent;
            String1Power.Foreground = accent;
            String2Power.Foreground = accent;
        }

        _activePalette = palette;
        ClientSettings.SaveTheme(theme);
    }

    private void Demo_Click(object sender, RoutedEventArgs e)
    {
        _pollTimer.Stop();
        if (_connected) WriteConnectionLog("Связь отключена: включён демонстрационный режим.");
        _connected = false;
        _connectionFailureLogged = false;
        ShowDemo();
    }

    private void ShowDemo()
    {
        DataModeText.Text = "  ·  демонстрационные данные";
        SystemStateText.Text = "Тестовые значения";
        TotalPowerText.Text = "1 842";
        TotalEnergyText.Text = "12 846,0";
        SetString(1, "1 084 Вт", "186,4 В", "5,82 А", "7 214,6 кВт·ч", "ДЕМО");
        SetString(2, "758 Вт", "182,1 В", "4,16 А", "5 631,4 кВт·ч", "ДЕМО");
        NetworkInfoText.Text = "Реальный контроллер не подключён. Нажмите «Подключить ESP32», чтобы начать чтение данных.";
        LastUpdateText.Text = "Обновлено: " + DateTime.Now.ToString("HH:mm:ss");
        PollInfoText.Text = "Опрос: выключен";
    }

    private async void Connect_Click(object sender, RoutedEventArgs e)
    {
        var raw = AddressBox.Text.Trim().TrimEnd('/');
        if (!Uri.TryCreate(raw, UriKind.Absolute, out var uri) ||
            (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps))
        {
            MessageBox.Show("Введите адрес контроллера, например http://192.168.4.1", "Solar_PV",
                MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        _baseAddress = uri.ToString().TrimEnd('/');
        ClientSettings.SaveControllerAddress(_baseAddress);
        WriteConnectionLog("Начата попытка подключения к " + _baseAddress);
        ConnectButton.IsEnabled = false;
        ConnectButton.Content = "Подключение…";
        try
        {
            await PollAsync();
            _pollTimer.Start();
        }
        finally
        {
            ConnectButton.IsEnabled = true;
            ConnectButton.Content = "Подключить ESP32";
        }
    }

    private async Task PollAsync()
    {
        if (_pollInProgress || string.IsNullOrWhiteSpace(_baseAddress)) return;
        _pollInProgress = true;
        try
        {
            using var response = await Http.GetAsync(_baseAddress + "/api/config");
            response.EnsureSuccessStatusCode();
            await using var stream = await response.Content.ReadAsStreamAsync();
            using var json = await JsonDocument.ParseAsync(stream);
            ApplyControllerData(json.RootElement);
            if (_connectionFailureLogged)
                WriteConnectionLog("Связь восстановлена с " + _baseAddress + ".");
            else if (!_connected)
                WriteConnectionLog("Связь установлена с " + _baseAddress + ".");
            _connected = true;
            _connectionFailureLogged = false;
            DataModeText.Text = "  ·  данные контроллера";
            SystemStateText.Text = "Связь установлена";
            NetworkInfoText.Text = "Получены данные от " + _baseAddress;
            PollInfoText.Text = "Опрос: каждые 3 сек.";
            LastUpdateText.Text = "Обновлено: " + DateTime.Now.ToString("HH:mm:ss");
        }
        catch (Exception ex)
        {
            if (_connected)
                WriteConnectionLog("Связь потеряна: " + ex.Message);
            else if (!_connectionFailureLogged)
                WriteConnectionLog("Контроллер пока недоступен: " + ex.Message);
            _connected = false;
            _connectionFailureLogged = true;
            DataModeText.Text = "  ·  ожидание контроллера";
            SystemStateText.Text = "Контроллер недоступен";
            NetworkInfoText.Text = "Нет ответа от /api/config: " + ex.Message + ". Повторная попытка через 3 сек.";
            PollInfoText.Text = "Автоподключение";
        }
        finally
        {
            _pollInProgress = false;
        }
    }

    private static void WriteConnectionLog(string message)
    {
        try
        {
            var directory = Path.GetDirectoryName(ConnectionLogPath);
            if (!string.IsNullOrEmpty(directory)) Directory.CreateDirectory(directory);
            File.AppendAllText(ConnectionLogPath,
                "[" + DateTimeOffset.Now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz") + "] " + message + Environment.NewLine);
        }
        catch
        {
            // Журнал не должен мешать работе мониторинга.
        }
    }

    private void OpenConnectionLog_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            if (!File.Exists(ConnectionLogPath))
                WriteConnectionLog("Журнал создан.");
            Process.Start(new ProcessStartInfo
            {
                FileName = "notepad.exe",
                Arguments = "\"" + ConnectionLogPath + "\"",
                UseShellExecute = false
            });
        }
        catch (Exception ex)
        {
            MessageBox.Show("Не удалось открыть журнал связи: " + ex.Message,
                "Solar_PV", MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void ApplyControllerData(JsonElement root)
    {
        if (root.TryGetProperty("totalEnergyWh", out var totalWh) && totalWh.TryGetDouble(out var wh))
            TotalEnergyText.Text = (wh / 1000.0).ToString("N1", CultureInfo.GetCultureInfo("ru-RU"));

        if (!root.TryGetProperty("stringTelemetry", out var telemetry) || telemetry.ValueKind != JsonValueKind.Array)
            return;

        SetString(1, "Нет данных", "— В", "— А", "— кВт·ч", "НЕТ ДАННЫХ");
        SetString(2, "Нет данных", "— В", "— А", "— кВт·ч", "НЕТ ДАННЫХ");
        double totalPower = 0;
        foreach (var item in telemetry.EnumerateArray())
        {
            if (!item.TryGetProperty("id", out var idNode) || !idNode.TryGetInt32(out var id) || id is < 1 or > 2)
                continue;
            var valid = item.TryGetProperty("valid", out var validNode) && validNode.ValueKind == JsonValueKind.True;
            var voltage = Number(item, "voltage");
            var current = Number(item, "current");
            var power = Number(item, "power");
            var energy = Number(item, "energyWh") / 1000.0;
            if (valid) totalPower += power;
            SetString(id,
                valid ? Format(power, "N0") + " Вт" : "Нет данных",
                Format(voltage, "N1") + " В",
                Format(current, "N2") + " А",
                Format(energy, "N3") + " кВт·ч",
                valid ? "НА СВЯЗИ" : "НЕТ ДАННЫХ");
        }
        TotalPowerText.Text = Format(totalPower, "N0");
    }

    private static double Number(JsonElement item, string name) =>
        item.TryGetProperty(name, out var value) && value.TryGetDouble(out var number) ? number : 0;

    private static string Format(double value, string format) =>
        value.ToString(format, CultureInfo.GetCultureInfo("ru-RU"));

    private void SetString(int id, string power, string voltage, string current, string energy, string state)
    {
        if (id == 1)
        {
            String1Power.Text = power; String1Voltage.Text = voltage;
            String1Current.Text = current; String1Energy.Text = energy; String1State.Text = state;
        }
        else
        {
            String2Power.Text = power; String2Voltage.Text = voltage;
            String2Current.Text = current; String2Energy.Text = energy; String2State.Text = state;
        }
    }
}