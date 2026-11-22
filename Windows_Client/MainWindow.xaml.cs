using System.Globalization;
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
    private string _baseAddress = "";

    public MainWindow()
    {
        InitializeComponent();
        UpdateWindowStateButton();
        var version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "—";
        VersionText.Text = $"Solar_PV Desktop {version}";
        AddressBox.Text = ClientSettings.LoadControllerAddress() ?? AddressBox.Text;
        ApplyTheme(ClientSettings.LoadTheme() ?? "Dark");
        _pollTimer.Tick += async (_, _) => await PollAsync();
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
            "Light" => new[] { "#F3F4F6", "#FFFFFF", "#20242A", "#667085", "#2563EB", "#E8EEF7", "#D0D5DD", "#E9EEF5" },
            "Office" => new[] { "#F5F7FA", "#FFFFFF", "#243247", "#68778C", "#4472C4", "#E8EEF7", "#D8E0EA", "#EDF2F8" },
            "Graphite" => new[] { "#202326", "#2B3035", "#F0F2F4", "#AAB2BC", "#E0A43A", "#E8EEF7", "#454D56", "#343B43" },
            "Blue" => new[] { "#10243D", "#183451", "#F0F6FC", "#A9C0D8", "#49A6E9", "#E8EEF7", "#315675", "#214565" },
            _ => new[] { "#0B1220", "#121E30", "#E8EEF7", "#8FA2BA", "#F5B942", "#E8EEF7", "#30445E", "#17263A" }
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
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.PaddingProperty, new Thickness(10, 6, 10, 6)));
        var highlighted = new Trigger
        {
            Property = System.Windows.Controls.MenuItem.IsHighlightedProperty,
            Value = true
        };
        highlighted.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush("#245A86")));
        highlighted.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush("#FFFFFF")));
        highlighted.Setters.Add(new Setter(System.Windows.Documents.TextElement.ForegroundProperty, PaletteBrush("#FFFFFF")));
        menuItemStyle.Triggers.Add(highlighted);
        var hovered = new Trigger
        {
            Property = System.Windows.UIElement.IsMouseOverProperty,
            Value = true
        };
        hovered.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush("#245A86")));
        hovered.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush("#FFFFFF")));
        hovered.Setters.Add(new Setter(System.Windows.Documents.TextElement.ForegroundProperty, PaletteBrush("#FFFFFF")));
        menuItemStyle.Triggers.Add(hovered);
        MainMenu.Resources[typeof(System.Windows.Controls.MenuItem)] = menuItemStyle;
        MainMenu.ItemContainerStyle = menuItemStyle;
        void Visit(DependencyObject parent)
        {
            for (var i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
            {
                var child = System.Windows.Media.VisualTreeHelper.GetChild(parent, i);
                if (child is System.Windows.Controls.Control control && control.Background is SolidColorBrush bg)
                    control.Background = ConvertBrush(bg);
                if (child is System.Windows.Controls.TextBlock text && text.Foreground is SolidColorBrush fg)
                    text.Foreground = ConvertBrush(fg);
                if (child is System.Windows.Controls.Border border && border.Background is SolidColorBrush borderBg)
                    border.Background = ConvertBrush(borderBg);
                Visit(child);
            }
        }
        Visit(this);
        ClientSettings.SaveTheme(theme);
    }

    private void Demo_Click(object sender, RoutedEventArgs e)
    {
        _pollTimer.Stop();
        _connected = false;
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
        ConnectButton.IsEnabled = false;
        ConnectButton.Content = "Подключение…";
        try
        {
            await PollAsync();
            if (_connected) _pollTimer.Start();
        }
        finally
        {
            ConnectButton.IsEnabled = true;
            ConnectButton.Content = "Подключить ESP32";
        }
    }

    private async Task PollAsync()
    {
        try
        {
            using var response = await Http.GetAsync(_baseAddress + "/api/config");
            response.EnsureSuccessStatusCode();
            await using var stream = await response.Content.ReadAsStreamAsync();
            using var json = await JsonDocument.ParseAsync(stream);
            ApplyControllerData(json.RootElement);
            _connected = true;
            DataModeText.Text = "  ·  данные контроллера";
            SystemStateText.Text = "Связь установлена";
            NetworkInfoText.Text = "Получены данные от " + _baseAddress;
            PollInfoText.Text = "Опрос: каждые 3 сек.";
            LastUpdateText.Text = "Обновлено: " + DateTime.Now.ToString("HH:mm:ss");
        }
        catch (Exception ex)
        {
            _pollTimer.Stop();
            _connected = false;
            DataModeText.Text = "  ·  ожидание контроллера";
            SystemStateText.Text = "Контроллер недоступен";
            NetworkInfoText.Text = "Не удалось получить /api/config: " + ex.Message;
            PollInfoText.Text = "Опрос: остановлен";
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