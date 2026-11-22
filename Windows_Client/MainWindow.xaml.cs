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
        _pollTimer.Tick += async (_, _) => await PollAsync();
        ShowDemo();
    }

    private void Demo_Click(object sender, RoutedEventArgs e)
    {
        _pollTimer.Stop();
        _connected = false;
        ShowDemo();
    }

    private void ShowDemo()
    {
        ConnectionText.Text = "ДЕМО РЕЖИМ";
        ConnectionText.Foreground = Brushes.Goldenrod;
        StatusDot.Fill = Brushes.Goldenrod;
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
            ConnectionText.Text = "ESP32 ПОДКЛЮЧЁН";
            ConnectionText.Foreground = Brushes.LightGreen;
            StatusDot.Fill = Brushes.LightGreen;
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
            ConnectionText.Text = "НЕТ СВЯЗИ";
            ConnectionText.Foreground = Brushes.IndianRed;
            StatusDot.Fill = Brushes.IndianRed;
            DataModeText.Text = "  ·  ожидание контроллера";
            SystemStateText.Text = "Контроллер недоступен";
            NetworkInfoText.Text = "Не удалось получить /api/config: " + ex.Message;
            PollInfoText.Text = "Опрос: остановлен";
        }
    }

    private void ApplyControllerData(JsonElement root)
    {
        if (root.TryGetProperty("totalEnergyWh", out var totalWh) &&
            totalWh.TryGetDouble(out var wh))
            TotalEnergyText.Text = (wh / 1000.0).ToString("N1", CultureInfo.GetCultureInfo("ru-RU"));

        if (!root.TryGetProperty("stringTelemetry", out var telemetry) || telemetry.ValueKind != JsonValueKind.Array)
            return;

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
