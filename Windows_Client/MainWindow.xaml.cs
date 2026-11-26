using System.Windows.Controls;
using System.Globalization;
using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;
using SharpVectors.Converters;

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
    private readonly Dictionary<string, System.Windows.Controls.Border> _energyMapItems = new();
    private Dictionary<string, string> _energyMapObjectBindings = ClientSettings.LoadEnergyMapObjectBindings();
    private List<EnergyMapElement> _energyMapElements = new();
    private bool _energyMapEditMode;
    private System.Windows.Controls.Border? _draggedEnergyMapItem;
    private System.Windows.Point _energyMapDragOffset;
    private string? _selectedEnergyMapObjectId;

    private bool _isAdminMode;
    private readonly Dictionary<string, string> _styleOverrides = ClientSettings.LoadStyleOverrides();

    private sealed record SystemStyleTarget(string Key, string Label, bool IsText);
    private string _activeTheme = "Dark";
    private string _activeDesign = "EnergyMap";
    private readonly HashSet<System.Windows.Controls.Button> _systemStyledButtons = new();
    private readonly Dictionary<System.Windows.Controls.Button, Brush> _systemButtonBaseBackgrounds = new();
    private readonly Dictionary<DependencyObject, Brush> _systemOriginalBackgrounds = new();
    private readonly Dictionary<DependencyObject, Brush> _systemOriginalForegrounds = new();
    private readonly Dictionary<DependencyObject, Brush> _systemOriginalBorderBrushes = new();
    private readonly Dictionary<DependencyObject, Brush> _systemOriginalTextBlockBackgrounds = new();
    private readonly Dictionary<DependencyObject, Brush> _systemOriginalShapeFills = new();
    private readonly Dictionary<DependencyObject, Brush> _systemOriginalShapeStrokes = new();

    public MainWindow()
    {
        InitializeComponent();
        EnsureDefaultSystemStyleOverrides();
        BuildEnergyMapItems();
        UpdateWindowStateButton();
        var version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "—";
        VersionText.Text = $"Solar_PV Desktop {version}";
        AddressBox.Text = ClientSettings.LoadControllerAddress() ?? AddressBox.Text;
        var savedTheme = ClientSettings.LoadTheme() ?? "Dark";
        var savedDesign = "EnergyMap";
        Loaded += (_, _) =>
        {
            ApplyTheme(savedTheme);
            ApplyDesign(savedDesign);
        };
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

    private void Admin_Click(object sender, RoutedEventArgs e)
    {
        if (_isAdminMode)
        {
            SetAdminMode(false);
            return;
        }

        if (ShowAdminLoginDialog())
            SetAdminMode(true);
    }

    private void SetAdminMode(bool enabled)
    {
        _isAdminMode = enabled;
        if (!enabled && _energyMapEditMode)
        {
            _energyMapEditMode = false;
            EnergyMapEditMenuItem.Header = "Перемещать";
            RenderEnergyMapElements();
            SaveEnergyMapLayout();
        }
        EnergyMapEditMenuItem.Visibility = enabled ? Visibility.Visible : Visibility.Collapsed;
        EnergyMapElementsMenuItem.Visibility = enabled ? Visibility.Visible : Visibility.Collapsed;
        StyleMenuItem.Visibility = enabled ? Visibility.Visible : Visibility.Collapsed;
        EnergyMapObjectHitCanvas.IsHitTestVisible = enabled && !_energyMapEditMode;
        UpdateEnergyMapHitTesting();
        UpdateEnergyMapSelection();
        if (!enabled)
        {
            _selectedEnergyMapObjectId = null;
        }
        Title = enabled ? "Solar_PV — Администратор" : "Solar_PV — Панель станции";
        WindowModeText.Text = enabled ? "Solar_PV · АДМИНИСТРАТОР" : "Solar_PV";
        AdminMenuItem.Header = enabled ? "Назад" : "Администратор";
        CreateMenuItem.Visibility = enabled ? Visibility.Visible : Visibility.Collapsed;
    }

    private bool ShowAdminLoginDialog()
    {
        var loginWindow = new Window
        {
            Title = "Вход администратора",
            Width = 420,
            Height = 310,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            ResizeMode = ResizeMode.NoResize,
            Owner = this,
            Background = GetSystemStyleBrush("background|dialog", "#121E30"),
            Foreground = GetSystemStyleBrush("text|dialog", "#E8EEF7")
        };

        var panel = new System.Windows.Controls.StackPanel { Margin = new Thickness(22) };
        panel.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = "РЕЖИМ АДМИНИСТРАТОРА",
            FontSize = 16,
            FontWeight = FontWeights.Bold,
            Margin = new Thickness(0, 0, 0, 16),
            Foreground = GetSystemStyleBrush("text|accent", "#F5B942")
        });

        panel.Children.Add(new System.Windows.Controls.TextBlock { Text = "Логин", Margin = new Thickness(0, 0, 0, 4) });
        var loginBox = new System.Windows.Controls.TextBox
        {
            Height = 30,
            Margin = new Thickness(0, 0, 0, 10),
            VerticalContentAlignment = VerticalAlignment.Center
        };
        panel.Children.Add(loginBox);

        panel.Children.Add(new System.Windows.Controls.TextBlock { Text = "Пароль", Margin = new Thickness(0, 0, 0, 4) });
        var passwordBox = new System.Windows.Controls.PasswordBox
        {
            Height = 30,
            Margin = new Thickness(0, 0, 0, 16)
        };
        panel.Children.Add(passwordBox);

        var buttons = new System.Windows.Controls.StackPanel
        {
            Orientation = System.Windows.Controls.Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right
        };
        var cancelButton = new System.Windows.Controls.Button
        {
            Content = "Отмена",
            Width = 90,
            Height = 32,
            Margin = new Thickness(0, 0, 8, 0)
        };
        cancelButton.Click += (_, _) => loginWindow.DialogResult = false;

        var enterButton = new System.Windows.Controls.Button
        {
            Content = "Войти",
            Width = 90,
            Height = 32,
            Background = GetSystemStyleBrush("background|button-primary", "#F5B942"),
            Foreground = GetSystemStyleBrush("text|button", "#17202D"),
            FontWeight = FontWeights.SemiBold,
            IsDefault = true
        };
        enterButton.Click += (_, _) => loginWindow.DialogResult = true;

        buttons.Children.Add(cancelButton);
        buttons.Children.Add(enterButton);
        panel.Children.Add(buttons);
        loginWindow.Content = panel;

        return loginWindow.ShowDialog() == true;
    }

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
    }

    private void Design_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not System.Windows.Controls.MenuItem item || item.Tag is not string design) return;
        if (design == "EnergyMap") ApplyDesign("EnergyMap");
    }

    private void EnsureDefaultSystemStyleOverrides()
    {
        string[] textKeys = { "primary", "secondary", "menu", "menu-selected", "button", "button-primary", "input", "disabled", "selection", "titlebar", "muted", "data", "accent", "success", "info", "warning", "error", "tooltip", "dialog" };
        string[] backgroundKeys = { "window", "panel", "menu", "dropdown", "menu-selected", "button", "button-primary", "button-hover", "button-pressed", "input", "selection", "titlebar", "dialog", "tooltip", "card", "card-alt", "surface-dark", "border", "accent", "info", "success", "warning", "error", "status-neutral", "titlebar-hover", "titlebar-pressed", "close-hover", "close-pressed" };
        foreach (var key in textKeys) _styleOverrides.TryAdd("system|text|" + key, "#FFFFFF");
        foreach (var key in backgroundKeys) _styleOverrides.TryAdd("system|background|" + key, "#3A3A3A");
        ClientSettings.SaveStyleOverrides(_styleOverrides);
    }

    private void StyleSubmenu_Opened(object sender, RoutedEventArgs e)
    {
        if (sender is not System.Windows.Controls.MenuItem menuItem) return;

        menuItem.Items.Clear();
        var isText = string.Equals(menuItem.Tag?.ToString(), "Text", StringComparison.Ordinal);
        var targets = isText
            ? new[]
            {
                new SystemStyleTarget("system|text|primary", "Основной текст интерфейса", true),
                new SystemStyleTarget("system|text|secondary", "Вторичный текст и подписи", true),
                new SystemStyleTarget("system|text|menu", "Текст пунктов меню", true),
                new SystemStyleTarget("system|text|menu-selected", "Текст выбранного пункта меню", true),
                new SystemStyleTarget("system|text|button", "Текст обычных кнопок", true),
                new SystemStyleTarget("system|text|button-primary", "Текст основных кнопок и контрастных состояний", true),
                new SystemStyleTarget("system|text|input", "Текст полей ввода", true),
                new SystemStyleTarget("system|text|disabled", "Текст неактивных элементов", true),
                new SystemStyleTarget("system|text|selection", "Текст выделенного элемента", true),
                new SystemStyleTarget("system|text|titlebar", "Текст заголовка окна", true),
                new SystemStyleTarget("system|text|muted", "Приглушённый и поясняющий текст", true),
                new SystemStyleTarget("system|text|data", "Текст показателей и значений", true),
                new SystemStyleTarget("system|text|accent", "Акцентный текст и подписи", true),
                new SystemStyleTarget("system|text|success", "Текст успешного состояния", true),
                new SystemStyleTarget("system|text|info", "Информационный текст", true),
                new SystemStyleTarget("system|text|warning", "Предупреждающий текст", true),
                new SystemStyleTarget("system|text|error", "Текст ошибки", true),
                new SystemStyleTarget("system|text|tooltip", "Текст всплывающих подсказок", true),
                new SystemStyleTarget("system|text|dialog", "Текст системных диалогов", true)
            }
            : new[]
            {
                new SystemStyleTarget("system|background|window", "Фон главного окна", false),
                new SystemStyleTarget("system|background|panel", "Фон панелей и карточек", false),
                new SystemStyleTarget("system|background|menu", "Фон главного меню", false),
                new SystemStyleTarget("system|background|dropdown", "Фон выпадающих меню и списков", false),
                new SystemStyleTarget("system|background|menu-selected", "Фон выбранного пункта меню", false),
                new SystemStyleTarget("system|background|button", "Фон обычных кнопок", false),
                new SystemStyleTarget("system|background|button-primary", "Фон основных кнопок", false),
                new SystemStyleTarget("system|background|button-hover", "Фон кнопок при наведении", false),
                new SystemStyleTarget("system|background|button-pressed", "Фон нажатых кнопок", false),
                new SystemStyleTarget("system|background|input", "Фон полей ввода", false),
                new SystemStyleTarget("system|background|selection", "Фон выделенного элемента", false),
                new SystemStyleTarget("system|background|titlebar", "Фон заголовка окна", false),
                new SystemStyleTarget("system|background|dialog", "Фон диалоговых окон", false),
                new SystemStyleTarget("system|background|tooltip", "Фон всплывающих подсказок", false),
                new SystemStyleTarget("system|background|card", "Фон информационных карточек", false),
                new SystemStyleTarget("system|background|card-alt", "Дополнительный фон карточек", false),
                new SystemStyleTarget("system|background|surface-dark", "Тёмный фон внутренних областей", false),
                new SystemStyleTarget("system|background|border", "Цвет рамок и разделителей", false),
                new SystemStyleTarget("system|background|accent", "Акцентный фон и индикаторы", false),
                new SystemStyleTarget("system|background|info", "Информационные элементы", false),
                new SystemStyleTarget("system|background|success", "Элементы успешного состояния", false),
                new SystemStyleTarget("system|background|warning", "Предупреждающие элементы", false),
                new SystemStyleTarget("system|background|error", "Элементы ошибки", false),
                new SystemStyleTarget("system|background|status-neutral", "Нейтральные индикаторы состояния", false),
                new SystemStyleTarget("system|background|titlebar-hover", "Кнопки заголовка при наведении", false),
                new SystemStyleTarget("system|background|titlebar-pressed", "Кнопки заголовка при нажатии", false),
                new SystemStyleTarget("system|background|close-hover", "Кнопка закрытия при наведении", false),
                new SystemStyleTarget("system|background|close-pressed", "Кнопка закрытия при нажатии", false)
            };

        foreach (var target in targets)
        {
            var entry = new System.Windows.Controls.MenuItem
            {
                Header = target.Label,
                Tag = target
            };
            entry.Click += StyleTarget_Click;
            menuItem.Items.Add(entry);
        }
    }

    private void StyleTarget_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not System.Windows.Controls.MenuItem item || item.Tag is not SystemStyleTarget target)
            return;

        var initialColor = _styleOverrides.TryGetValue(target.Key, out var savedColor)
            ? savedColor
            : target.IsText ? "#FFFFFF" : "#3A3A3A";

        var selectedColor = ShowStyleColorDialog(
            (target.IsText ? "Цвет текста — " : "Цвет фона — ") + target.Label,
            initialColor);
        if (selectedColor is null ||
            System.Windows.Media.ColorConverter.ConvertFromString(selectedColor) is not System.Windows.Media.Color)
            return;

        _styleOverrides[target.Key] = selectedColor;
        ClientSettings.SaveStyleOverrides(_styleOverrides);
        ApplyTheme(_activeTheme);
    }

    private string? ShowStyleColorDialog(string title, string initialColor)
    {
        var dialog = new Window
        {
            Title = title,
            Width = 520,
            Height = 500,
            MinWidth = 520,
            MinHeight = 500,
            ResizeMode = ResizeMode.CanMinimize,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Owner = this,
            Background = GetSystemStyleBrush("background|dialog", "#121E30"),
            Foreground = GetSystemStyleBrush("text|dialog", "#E8EEF7"),
            ShowInTaskbar = false
        };

        string selectedColor = initialColor;
        var root = new DockPanel { Margin = new Thickness(16) };
        var bottom = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 14, 0, 0)
        };
        DockPanel.SetDock(bottom, Dock.Bottom);

        var previewRow = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            VerticalAlignment = VerticalAlignment.Center,
            Margin = new Thickness(0, 0, 0, 12)
        };
        var preview = new Border
        {
            Width = 72,
            Height = 34,
            CornerRadius = new CornerRadius(4),
            BorderThickness = new Thickness(1),
            BorderBrush = Brushes.White,
            Background = new SolidColorBrush((Color)ColorConverter.ConvertFromString(initialColor)),
            Margin = new Thickness(0, 0, 12, 0)
        };
        var selectedName = new TextBlock
        {
            Text = "Выбранный цвет",
            VerticalAlignment = VerticalAlignment.Center,
            FontSize = 14
        };
        previewRow.Children.Add(preview);
        previewRow.Children.Add(selectedName);
        DockPanel.SetDock(previewRow, Dock.Top);

        var paletteScroll = new ScrollViewer
        {
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
        };
        var palette = new WrapPanel { Orientation = Orientation.Horizontal };

        var colors = new (string Name, string Hex)[]
        {
            ("Белый", "#FFFFFFFF"), ("Слоновая кость", "#FFFFF8E7"), ("Светло-серый", "#FFD3D3D3"),
            ("Серый", "#FF808080"), ("Тёмно-серый", "#FF505050"), ("Чёрный", "#FF000000"),
            ("Красный", "#FFFF0000"), ("Тёмно-красный", "#FF8B0000"), ("Коралловый", "#FFFF7F50"),
            ("Розовый", "#FFFFC0CB"), ("Малиновый", "#FFC71585"), ("Оранжевый", "#FFFFA500"),
            ("Янтарный", "#FFFFBF00"), ("Жёлтый", "#FFFFFF00"), ("Золотой", "#FFFFD700"),
            ("Лаймовый", "#FFBFFF00"), ("Салатовый", "#FF7CFC00"), ("Зелёный", "#FF008000"),
            ("Тёмно-зелёный", "#FF006400"), ("Мятный", "#FF98FF98"), ("Бирюзовый", "#FF40E0D0"),
            ("Голубой", "#FF87CEEB"), ("Небесно-синий", "#FF00BFFF"), ("Синий", "#FF0000FF"),
            ("Тёмно-синий", "#FF00008B"), ("Индиго", "#FF4B0082"), ("Фиолетовый", "#FF800080"),
            ("Сиреневый", "#FFDDA0DD"), ("Коричневый", "#FFA52A2A"), ("Бежевый", "#FFF5F5DC")
        };

        foreach (var entry in colors)
        {
            var swatch = new Button
            {
                Width = 64,
                Height = 48,
                Margin = new Thickness(3),
                Padding = new Thickness(2),
                Background = new SolidColorBrush((Color)ColorConverter.ConvertFromString(entry.Hex)),
                BorderBrush = Brushes.Gray,
                BorderThickness = new Thickness(1),
                ToolTip = entry.Name
            };
            swatch.Click += (_, _) =>
            {
                selectedColor = entry.Hex;
                preview.Background = new SolidColorBrush((Color)ColorConverter.ConvertFromString(entry.Hex));
                selectedName.Text = entry.Name;
            };
            palette.Children.Add(swatch);
        }

        paletteScroll.Content = palette;

        var custom = new Expander
        {
            Header = "Другой цвет (дополнительно)",
            Margin = new Thickness(0, 12, 0, 0),
            Foreground = Brushes.White
        };
        var customPanel = new StackPanel { Orientation = Orientation.Horizontal };
        var customBox = new TextBox
        {
            Text = initialColor,
            Width = 130,
            Height = 28,
            VerticalContentAlignment = VerticalAlignment.Center,
            Margin = new Thickness(0, 0, 8, 0)
        };
        var customApply = new Button
        {
            Content = "Выбрать",
            Width = 86,
            Height = 28
        };
        customApply.Click += (_, _) =>
        {
            try
            {
                if (ColorConverter.ConvertFromString(customBox.Text.Trim()) is Color color)
                {
                    selectedColor = color.ToString();
                    preview.Background = new SolidColorBrush(color);
                    selectedName.Text = "Пользовательский цвет";
                }
                else
                    MessageBox.Show(dialog, "Не удалось распознать цвет.", "Стиль",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
            }
            catch
            {
                MessageBox.Show(dialog, "Не удалось распознать цвет.", "Стиль",
                    MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        };
        customPanel.Children.Add(customBox);
        customPanel.Children.Add(customApply);
        custom.Content = customPanel;

        var cancel = new Button
        {
            Content = "Отмена",
            Width = 92,
            Height = 32,
            Margin = new Thickness(0, 0, 8, 0),
            IsCancel = true
        };
        cancel.Click += (_, _) => dialog.DialogResult = false;
        var apply = new Button
        {
            Content = "Применить",
            Width = 100,
            Height = 32,
            IsDefault = true
        };
        apply.Click += (_, _) => dialog.DialogResult = true;
        bottom.Children.Add(cancel);
        bottom.Children.Add(apply);

        var content = new StackPanel();
        content.Children.Add(previewRow);
        content.Children.Add(paletteScroll);
        content.Children.Add(custom);
        root.Children.Add(bottom);
        root.Children.Add(content);
        dialog.Content = root;

        return dialog.ShowDialog() == true ? selectedColor : null;
    }

    private Brush GetSystemStyleBrush(string key, string fallback)
    {
        var value = _styleOverrides.TryGetValue("system|" + key, out var saved) ? saved : fallback;
        return new SolidColorBrush((Color)ColorConverter.ConvertFromString(value));
    }

    private void RegisterSystemButton(System.Windows.Controls.Button button)
    {
        _systemButtonBaseBackgrounds[button] = button.Background;
        if (!_systemStyledButtons.Add(button)) return;

        button.MouseEnter += (_, _) =>
        {
            if (button.IsEnabled)
                button.Background = GetSystemStyleBrush("background|button-hover", "#31445E");
        };
        button.MouseLeave += (_, _) =>
        {
            if (_systemButtonBaseBackgrounds.TryGetValue(button, out var background))
                button.Background = background;
        };
        button.PreviewMouseLeftButtonDown += (_, _) =>
        {
            if (button.IsEnabled)
                button.Background = GetSystemStyleBrush("background|button-pressed", "#405875");
        };
        button.PreviewMouseLeftButtonUp += (_, _) =>
        {
            if (!button.IsEnabled) return;
            button.Background = button.IsMouseOver
                ? GetSystemStyleBrush("background|button-hover", "#31445E")
                : _systemButtonBaseBackgrounds[button];
        };
    }

    private void ApplyStyleOverrides()
    {
        // System color overrides are applied centrally by ApplyTheme.
        // Workspace objects keep their own independent appearance settings.
    }

    private void ApplyTheme(string theme)
    {
        _activeTheme = theme;
        var palette = theme switch
        {
            "Light" => new[] { "#F2F2F2", "#FFFFFF", "#252525", "#666666", "#555555", "#E8EEF7", "#D6D6D6", "#E5E5E5" },
            "Office" => new[] { "#DCE6F1", "#F3F6FA", "#1F3864", "#526780", "#0078D4", "#E8EEF7", "#B4C7E7", "#C9D9EC" },
            "Graphite" => new[] { "#202326", "#2B3035", "#F0F2F4", "#AAB2BC", "#E0A43A", "#E8EEF7", "#454D56", "#343B43" },
            "Blue" => new[] { "#10243D", "#183451", "#F0F6FC", "#A9C0D8", "#49A6E9", "#E8EEF7", "#315675", "#214565" },
            _ => new[] { "#0B1220", "#121E30", "#E8EEF7", "#8FA2BA", "#F5B942", "#E8EEF7", "#30445E", "#17263A" }
        };

        string StyleColor(string key, string fallback) =>
            _styleOverrides.TryGetValue("system|" + key, out var value) ? value : fallback;

        palette[0] = StyleColor("background|window", "#3A3A3A");
        palette[1] = StyleColor("background|panel", "#3A3A3A");
        palette[2] = StyleColor("text|primary", "#FFFFFF");
        palette[3] = StyleColor("text|secondary", "#FFFFFF");
        palette[4] = StyleColor("text|accent", "#FFFFFF");
        palette[6] = StyleColor("background|border", "#555555");
        palette[7] = StyleColor("background|menu", "#3A3A3A");

        var menuTextColor = StyleColor("text|menu", "#FFFFFF");
        var menuSelectedTextColor = StyleColor("text|menu-selected", "#FFFFFF");
        var menuSelectedBackground = StyleColor("background|menu-selected", "#3A3A3A");
        var dropdownBackground = StyleColor("background|dropdown", "#3A3A3A");
        var buttonTextColor = StyleColor("text|button", "#FFFFFF");
        var inputTextColor = StyleColor("text|input", "#FFFFFF");
        var disabledTextColor = StyleColor("text|disabled", "#FFFFFF");
        var buttonBackground = StyleColor("background|button", "#3A3A3A");
        var primaryButtonBackground = StyleColor("background|button-primary", "#3A3A3A");
        var buttonHoverBackground = StyleColor("background|button-hover", "#3A3A3A");
        var buttonPressedBackground = StyleColor("background|button-pressed", "#3A3A3A");
        var inputBackground = StyleColor("background|input", "#3A3A3A");

        var map = new Dictionary<string, string>
        {
            ["#FF0B1220"] = palette[0], ["#FF121E30"] = palette[1],
            ["#FFE8EEF7"] = palette[2], ["#FF8FA2BA"] = palette[3],
            ["#FFF5B942"] = StyleColor("text|accent", "#FFFFFF"),
            ["#FF17263A"] = StyleColor("background|dropdown", "#3A3A3A"),
            ["#FF24364D"] = StyleColor("background|menu", "#3A3A3A"),
            ["#FF30445E"] = palette[6],
            ["#FF26384D"] = buttonBackground, ["#FF405875"] = buttonPressedBackground,
            ["#FF1A2B3E"] = palette[1],
            ["#FF31445E"] = buttonHoverBackground,
            ["#FFDCE6F2"] = menuSelectedBackground,
            ["#FFF4F7FB"] = StyleColor("text|titlebar", "#FFFFFF"),
            ["#FFAAB4C0"] = disabledTextColor,
            ["#FFF0F5FA"] = StyleColor("text|data", "#FFFFFF"),
            ["#FFAFC1D2"] = StyleColor("text|muted", "#FFFFFF"),
            ["#FF71849B"] = StyleColor("text|muted", "#FFFFFF"),
            ["#FF63D6A2"] = StyleColor("text|success", "#FFFFFF"),
            ["#FF63B9F2"] = StyleColor("text|info", "#FFFFFF"),
            ["#FFE45B5B"] = StyleColor("text|error", "#FFFFFF"),
            ["#FF64748B"] = StyleColor("background|status-neutral", "#3A3A3A"),
            ["#FF17202D"] = StyleColor("text|button-primary", "#FFFFFF"),
            ["#FF172B40"] = StyleColor("background|card", "#3A3A3A"),
            ["#FF1A3047"] = StyleColor("background|card-alt", "#3A3A3A"),
            ["#FF0B1725"] = StyleColor("background|surface-dark", "#3A3A3A"),
            ["#FF07111D"] = StyleColor("background|surface-dark", "#3A3A3A"),
            ["#FF31516B"] = StyleColor("background|border", "#555555"),
            ["#FF45617A"] = StyleColor("background|border", "#555555"),
            ["#FF247DB5"] = StyleColor("background|info", "#3A3A3A"),
            ["#FFC42B1C"] = StyleColor("background|close-hover", "#3A3A3A"),
            ["#FF9E2116"] = StyleColor("background|close-pressed", "#3A3A3A")
        };

        if (_activePalette is not null)
        {
            int[] paletteSlots = { 0, 1, 2, 3, 4, 6, 7 };
            foreach (var slot in paletteSlots)
                map["#FF" + _activePalette[slot].TrimStart('#')] = palette[slot];
        }

        Brush ConvertBrush(Brush brush)
        {
            if (brush is SolidColorBrush solid && map.TryGetValue(solid.Color.ToString(), out var replacementColor))
                return new SolidColorBrush((Color)ColorConverter.ConvertFromString(replacementColor));
            return brush;
        }

        SolidColorBrush PaletteBrush(string value) =>
            new((Color)ColorConverter.ConvertFromString(value));

        Background = PaletteBrush(palette[0]);
        Foreground = PaletteBrush(palette[2]);
        MainMenu.Background = PaletteBrush(palette[7]);
        MainMenu.Foreground = PaletteBrush(palette[2]);

        var menuItemStyle = new Style(typeof(System.Windows.Controls.MenuItem));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush(dropdownBackground)));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush(menuTextColor)));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.MenuItem.HeaderTemplateProperty,
            (DataTemplate)FindResource("MenuItemHeaderTemplate")));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.PaddingProperty, new Thickness(10, 6, 10, 6)));
        var highlighted = new Trigger { Property = System.Windows.Controls.MenuItem.IsHighlightedProperty, Value = true };
        highlighted.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush(menuSelectedBackground)));
        highlighted.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush(menuSelectedTextColor)));
        menuItemStyle.Triggers.Add(highlighted);
        var hovered = new Trigger { Property = System.Windows.UIElement.IsMouseOverProperty, Value = true };
        hovered.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush(menuSelectedBackground)));
        hovered.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush(menuSelectedTextColor)));
        menuItemStyle.Triggers.Add(hovered);
        var disabledMenuItem = new Trigger
        {
            Property = System.Windows.UIElement.IsEnabledProperty,
            Value = false
        };
        disabledMenuItem.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty,
            PaletteBrush(disabledTextColor)));
        menuItemStyle.Triggers.Add(disabledMenuItem);
        MainMenu.Resources[typeof(System.Windows.Controls.MenuItem)] = menuItemStyle;
        Application.Current.Resources[typeof(System.Windows.Controls.MenuItem)] = menuItemStyle;
        MainMenu.ItemContainerStyle = menuItemStyle;

        var listItemStyle = new Style(typeof(System.Windows.Controls.ListBoxItem));
        listItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush(palette[2])));
        var selectedItem = new Trigger
        {
            Property = System.Windows.Controls.ListBoxItem.IsSelectedProperty,
            Value = true
        };
        selectedItem.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty,
            PaletteBrush(StyleColor("background|selection", "#DCE6F2"))));
        selectedItem.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty,
            PaletteBrush(StyleColor("text|selection", "#17202D"))));
        listItemStyle.Triggers.Add(selectedItem);
        var disabledListItem = new Trigger { Property = System.Windows.UIElement.IsEnabledProperty, Value = false };
        disabledListItem.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush(disabledTextColor)));
        listItemStyle.Triggers.Add(disabledListItem);
        Application.Current.Resources[typeof(System.Windows.Controls.ListBoxItem)] = listItemStyle;

        var comboItemStyle = new Style(typeof(System.Windows.Controls.ComboBoxItem));
        comboItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush(dropdownBackground)));
        comboItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush(inputTextColor)));
        var comboHighlighted = new Trigger
        {
            Property = System.Windows.UIElement.IsMouseOverProperty,
            Value = true
        };
        comboHighlighted.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty,
            PaletteBrush(StyleColor("background|selection", "#DCE6F2"))));
        comboHighlighted.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty,
            PaletteBrush(StyleColor("text|selection", "#17202D"))));
        comboItemStyle.Triggers.Add(comboHighlighted);
        var disabledComboItem = new Trigger { Property = System.Windows.UIElement.IsEnabledProperty, Value = false };
        disabledComboItem.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush(disabledTextColor)));
        comboItemStyle.Triggers.Add(disabledComboItem);
        Application.Current.Resources[typeof(System.Windows.Controls.ComboBoxItem)] = comboItemStyle;

        void ApplyMenuItemStyle(System.Windows.Controls.MenuItem item)
        {
            item.Style = menuItemStyle;
            foreach (var child in item.Items)
                if (child is System.Windows.Controls.MenuItem childMenuItem)
                    ApplyMenuItemStyle(childMenuItem);
        }
        foreach (var menuEntry in MainMenu.Items)
            if (menuEntry is System.Windows.Controls.MenuItem menuItem)
                ApplyMenuItemStyle(menuItem);

        void Visit(DependencyObject parent)
        {
            // Workspace objects have their own independent color settings.
            if (ReferenceEquals(parent, EnergyMapDashboard))
                return;

            for (var i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
            {
                var child = System.Windows.Media.VisualTreeHelper.GetChild(parent, i);
                if (child is System.Windows.Controls.Control control)
                {
                    if (control.Background is SolidColorBrush currentBackground &&
                        !_systemOriginalBackgrounds.ContainsKey(control))
                        _systemOriginalBackgrounds[control] = currentBackground;
                    if (control.Foreground is SolidColorBrush currentForeground &&
                        !_systemOriginalForegrounds.ContainsKey(control))
                        _systemOriginalForegrounds[control] = currentForeground;

                    if (control is System.Windows.Controls.TextBox or System.Windows.Controls.PasswordBox or System.Windows.Controls.ComboBox)
                    {
                        control.Background = PaletteBrush(inputBackground);
                        control.Foreground = PaletteBrush(inputTextColor);
                    }
                    else
                    {
                        if (control is System.Windows.Controls.Button button &&
                            _systemOriginalBackgrounds.TryGetValue(button, out var originalButtonBackground) &&
                            originalButtonBackground is SolidColorBrush buttonBg)
                        {
                            if (buttonBg.Color.ToString() == "#FFF5B942")
                                button.Background = PaletteBrush(primaryButtonBackground);
                            else if (buttonBg.Color.ToString() == "#FF26384D")
                                button.Background = PaletteBrush(buttonBackground);
                            else
                                button.Background = ConvertBrush(buttonBg);

                            if (button.Style != (TryFindResource("CloseTitleBarButtonStyle") as Style))
                                RegisterSystemButton(button);
                        }
                        else if (_systemOriginalBackgrounds.TryGetValue(control, out var originalBackground))
                            control.Background = ConvertBrush(originalBackground);

                        if (_systemOriginalForegrounds.TryGetValue(control, out var originalForeground))
                        {
                            if (control is System.Windows.Controls.Button &&
                                originalForeground is SolidColorBrush buttonForeground &&
                                (buttonForeground.Color.ToString() == "#FFE8EEF7" || buttonForeground.Color.ToString() == "#FFF4F7FB"))
                                control.Foreground = PaletteBrush(buttonTextColor);
                            else
                                control.Foreground = ConvertBrush(originalForeground);
                        }
                    }
                }
                if (child is System.Windows.Controls.TextBlock textBlock)
                {
                    if (textBlock.Foreground is SolidColorBrush currentTextForeground &&
                        !_systemOriginalForegrounds.ContainsKey(textBlock))
                        _systemOriginalForegrounds[textBlock] = currentTextForeground;
                    if (_systemOriginalForegrounds.TryGetValue(textBlock, out var originalTextForeground))
                        textBlock.Foreground = ConvertBrush(originalTextForeground);

                    if (textBlock.Background is SolidColorBrush currentTextBackground &&
                        !_systemOriginalTextBlockBackgrounds.ContainsKey(textBlock))
                        _systemOriginalTextBlockBackgrounds[textBlock] = currentTextBackground;
                    if (_systemOriginalTextBlockBackgrounds.TryGetValue(textBlock, out var originalTextBackground))
                        textBlock.Background = ConvertBrush(originalTextBackground);
                }
                if (child is System.Windows.Shapes.Shape shape)
                {
                    if (shape.Fill is SolidColorBrush currentFill &&
                        !_systemOriginalShapeFills.ContainsKey(shape))
                        _systemOriginalShapeFills[shape] = currentFill;
                    if (_systemOriginalShapeFills.TryGetValue(shape, out var originalFill))
                        shape.Fill = ConvertBrush(originalFill);

                    if (shape.Stroke is SolidColorBrush currentStroke &&
                        !_systemOriginalShapeStrokes.ContainsKey(shape))
                        _systemOriginalShapeStrokes[shape] = currentStroke;
                    if (_systemOriginalShapeStrokes.TryGetValue(shape, out var originalStroke))
                        shape.Stroke = ConvertBrush(originalStroke);
                }
                if (child is System.Windows.Controls.Panel panel)
                {
                    if (panel.Background is SolidColorBrush currentPanelBackground &&
                        !_systemOriginalBackgrounds.ContainsKey(panel))
                        _systemOriginalBackgrounds[panel] = currentPanelBackground;
                    if (_systemOriginalBackgrounds.TryGetValue(panel, out var originalPanelBackground))
                        panel.Background = ConvertBrush(originalPanelBackground);
                }
                if (child is System.Windows.Controls.Border border)
                {
                    if (border.Background is SolidColorBrush currentBorderBackground &&
                        !_systemOriginalBackgrounds.ContainsKey(border))
                        _systemOriginalBackgrounds[border] = currentBorderBackground;
                    if (_systemOriginalBackgrounds.TryGetValue(border, out var originalBorderBackground))
                        border.Background = ConvertBrush(originalBorderBackground);
                    if (border.BorderBrush is SolidColorBrush currentBorderBrush &&
                        !_systemOriginalBorderBrushes.ContainsKey(border))
                        _systemOriginalBorderBrushes[border] = currentBorderBrush;
                    if (_systemOriginalBorderBrushes.TryGetValue(border, out var originalBorderBrush))
                        border.BorderBrush = ConvertBrush(originalBorderBrush);
                }
                Visit(child);
            }
        }
        Visit(this);

        Resources["SystemButtonHoverBrush"] = PaletteBrush(StyleColor("background|titlebar-hover", buttonHoverBackground));
        Resources["SystemButtonPressedBrush"] = PaletteBrush(StyleColor("background|titlebar-pressed", buttonPressedBackground));
        Resources["SystemCloseHoverBrush"] = PaletteBrush(StyleColor("background|close-hover", "#3A3A3A"));
        Resources["SystemClosePressedBrush"] = PaletteBrush(StyleColor("background|close-pressed", "#3A3A3A"));

        var toolTipStyle = new Style(typeof(System.Windows.Controls.ToolTip));
        toolTipStyle.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty,
            PaletteBrush(StyleColor("background|tooltip", "#17263A"))));
        toolTipStyle.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty,
            PaletteBrush(StyleColor("text|tooltip", "#F4F7FB"))));
        toolTipStyle.Setters.Add(new Setter(System.Windows.Controls.Control.BorderBrushProperty,
            PaletteBrush(StyleColor("background|border", "#31516B"))));
        Application.Current.Resources[typeof(System.Windows.Controls.ToolTip)] = toolTipStyle;

        _activePalette = palette;
        ClientSettings.SaveTheme(theme);
        ApplyDesign(_activeDesign);

        // ApplyDesign adjusts card borders and the window background; apply user color
        // overrides once more so the selected system colors take precedence immediately.
        TitleBarGrid.Background = PaletteBrush(StyleColor("background|titlebar", "#3A3A3A"));
        MainMenu.Background = PaletteBrush(StyleColor("background|menu", "#3A3A3A"));
        Visit(this);

        ApplyStyleOverrides();
    }

    private void ApplyDesign(string design)
    {
        if (design is not ("Digital" or "EnergyMap" or "Flow" or "Blueprint"))
            design = "Digital";
        _activeDesign = design;

        var palette = _activePalette ?? new[] { "#0B1220", "#121E30", "#E8EEF7", "#8FA2BA", "#F5B942", "#E8EEF7", "#30445E", "#17263A" };
        DigitalDashboard.Visibility = design == "Digital" ? Visibility.Visible : Visibility.Collapsed;
        EnergyMapDashboard.Visibility = design == "EnergyMap" ? Visibility.Visible : Visibility.Collapsed;
        if (design == "EnergyMap")
        {
            DashboardContentGrid.SetBinding(System.Windows.FrameworkElement.WidthProperty,
                new System.Windows.Data.Binding(nameof(System.Windows.Controls.ScrollViewer.ViewportWidth)) { Source = DashboardScrollViewer });
            DashboardContentGrid.SetBinding(System.Windows.FrameworkElement.HeightProperty,
                new System.Windows.Data.Binding(nameof(System.Windows.Controls.ScrollViewer.ViewportHeight)) { Source = DashboardScrollViewer });
        }
        else
        {
            DashboardContentGrid.ClearValue(System.Windows.FrameworkElement.WidthProperty);
            DashboardContentGrid.ClearValue(System.Windows.FrameworkElement.HeightProperty);
        }
        LegacyDashboard.Visibility = design is "Digital" or "EnergyMap" ? Visibility.Collapsed : Visibility.Visible;
        var accent = GetSystemStyleBrush("text|accent", palette[4]);
        var borderBrush = GetSystemStyleBrush("background|border", palette[6]);

        var cornerRadius = design switch
        {
            "Flow" => new CornerRadius(1),
            "Blueprint" => new CornerRadius(0),
            _ => new CornerRadius(4)
        };

        void StyleCards(DependencyObject parent)
        {
            if (ReferenceEquals(parent, EnergyMapObjectHitCanvas) ||
                ReferenceEquals(parent, EnergyMapItemsCanvas))
                return;

            for (var i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
            {
                var child = System.Windows.Media.VisualTreeHelper.GetChild(parent, i);
                if (child is System.Windows.Controls.Border border)
                {
                    border.CornerRadius = cornerRadius;
                    border.BorderBrush = design == "Flow" ? accent : borderBrush;
                    border.BorderThickness = design == "Flow" ? new Thickness(3, 1, 1, 1)
                        : new Thickness(design == "Blueprint" ? 1.2 : 1);
                }
                StyleCards(child);
            }
        }
        StyleCards(this);

        var stringCards = StringCardsGrid.Children.OfType<System.Windows.Controls.Border>().ToArray();
        StringCardsGrid.ColumnDefinitions.Clear();
        StringCardsGrid.RowDefinitions.Clear();
        if (design == "Flow")
        {
            StringCardsGrid.ColumnDefinitions.Add(new System.Windows.Controls.ColumnDefinition());
            for (var i = 0; i < stringCards.Length; i++)
                StringCardsGrid.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = GridLength.Auto });
            for (var i = 0; i < stringCards.Length; i++)
            {
                System.Windows.Controls.Grid.SetColumn(stringCards[i], 0);
                System.Windows.Controls.Grid.SetRow(stringCards[i], i);
            }
        }
        else
        {
            StringCardsGrid.ColumnDefinitions.Add(new System.Windows.Controls.ColumnDefinition());
            StringCardsGrid.ColumnDefinitions.Add(new System.Windows.Controls.ColumnDefinition());
            StringCardsGrid.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = GridLength.Auto });
            for (var i = 0; i < stringCards.Length; i++)
            {
                System.Windows.Controls.Grid.SetColumn(stringCards[i], i);
                System.Windows.Controls.Grid.SetRow(stringCards[i], 0);
            }
        }

        if (design == "Blueprint")
        {
            var lines = new GeometryGroup();
            lines.Children.Add(new LineGeometry(new Point(0, 0), new Point(32, 0)));
            lines.Children.Add(new LineGeometry(new Point(0, 0), new Point(0, 32)));
            var drawing = new DrawingGroup();
            drawing.Children.Add(new GeometryDrawing(
                new SolidColorBrush((Color)ColorConverter.ConvertFromString(palette[0])),
                null,
                new RectangleGeometry(new Rect(0, 0, 32, 32))));
            drawing.Children.Add(new GeometryDrawing(null, new Pen(borderBrush, 0.45), lines));
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
        else
        {
            Background = new SolidColorBrush((Color)ColorConverter.ConvertFromString(palette[0]));
        }

        var valueFont = design switch
        {
            "Blueprint" => new FontFamily("Consolas"),
            _ => new FontFamily("Segoe UI")
        };
        TotalPowerText.FontFamily = valueFont;
        TotalEnergyText.FontFamily = valueFont;
        String1Power.FontFamily = valueFont;
        String2Power.FontFamily = valueFont;
        TotalPowerText.FontSize = 34;
        TotalEnergyText.FontSize = 34;
        String1Power.FontSize = 29;
        String2Power.FontSize = 29;

        TotalPowerText.Foreground = accent;
        String1Power.Foreground = accent;
        String2Power.Foreground = accent;

        ClientSettings.SaveDesign(design);
        ApplyStyleOverrides();
    }

    private void BuildEnergyMapItems()
    {
        var resource = Application.GetResourceStream(new Uri("pack://application:,,,/Screenshot_1_vector.svg"));
        if (resource is null) throw new InvalidOperationException("Не найден ресурс энергосхемы SVG.");
        string source;
        using (resource.Stream)
        using (var reader = new StreamReader(resource.Stream))
            source = reader.ReadToEnd();

        var defaults = EnergyMapSvgDocument.CreateDefaults(source, out var backgroundSvg);
        _energyMapElements = ClientSettings.LoadEnergyMapElements();
        if (_energyMapElements.Count == 0)
        {
            _energyMapElements = defaults;
            var pv1 = _energyMapElements.FirstOrDefault(item => item.Kind == "Indicator" && item.Text.Contains("0.31", StringComparison.Ordinal));
            var pv2 = _energyMapElements.FirstOrDefault(item => item.Kind == "Indicator" && item.Text.Contains("0.30", StringComparison.Ordinal));
            if (pv1 is not null) pv1.DataSource = "Pv1Power";
            if (pv2 is not null) pv2.DataSource = "Pv2Power";
            ClientSettings.SaveEnergyMapElements(_energyMapElements);
        }

        var svg = new SvgViewbox
        {
            Width = 1409,
            Height = 905,
            Stretch = Stretch.Fill,
            IsHitTestVisible = false
        };
        svg.Load(backgroundSvg, false);
        EnergyMapSvgHost.Children.Clear();
        EnergyMapSvgHost.Children.Add(svg);
        BuildEnergyMapObjectTargets();
        RenderEnergyMapElements();
    }

    private void BuildEnergyMapObjectTargets()
    {
        EnergyMapObjectHitCanvas.Children.Clear();

        var objects = new (string Id, string Name, double X, double Y, double Width, double Height, string? Path)[]
        {
            ("title", "Заголовок схемы", 410, 12, 588, 68, null),
            ("sun", "Солнце", 45, 22, 120, 120, null),
            ("house", "Дом", 4, 82, 410, 352, null),
            ("grid", "ЛЭП", 1131, 80, 278, 355, null),
            ("inverter", "Инвертор", 581, 115, 280, 336, null),
            ("pv1", "Солнечная панель 1", 14, 513, 260, 335, null),
            ("pv2", "Солнечная панель 2", 275, 513, 260, 335, null),
            ("battery", "Аккумулятор", 1030, 530, 323, 320, null),
            ("arrow-house", "Поток энергии: инвертор — дом", 373, 284, 207, 53, "M580,331 L373,331"),
            ("arrow-grid", "Поток энергии: ЛЭП — инвертор", 865, 284, 265, 53, "M1130,331 L865,331"),
            ("arrow-pv1", "Поток энергии: панель 1 — инвертор", 250, 454, 398, 396, "M250,850 H570 Q648,850 648,772 V454"),
            ("arrow-pv2", "Поток энергии: панель 2 — инвертор", 520, 454, 180, 336, "M520,790 H700 V705 V454"),
            ("arrow-battery-out", "Поток энергии: инвертор — аккумулятор", 813, 454, 212, 249, "M813,454 V660 Q813,703 857,703 H1025"),
            ("arrow-battery-in", "Поток энергии: аккумулятор — инвертор", 779, 454, 246, 297, "M779,454 V735 Q779,751 822,751 H1025"),
            ("arrow-house", "Поток энергии: инвертор — дом", 411, 284, 132, 43, null),
            ("arrow-grid", "Поток энергии: ЛЭП — инвертор", 944, 283, 130, 43, null),
            ("arrow-pv1", "Поток энергии: панель 1 — инвертор", 520, 848, 112, 43, null),
            ("arrow-pv2", "Поток энергии: панель 2 — инвертор", 520, 735, 112, 43, null),
            ("arrow-battery-out", "Поток энергии: инвертор — аккумулятор", 833, 537, 122, 43, null),
            ("arrow-battery-in", "Поток энергии: аккумулятор — инвертор", 833, 765, 122, 43, null)
        };

        foreach (var item in objects)
        {
            System.Windows.FrameworkElement target;
            if (item.Path is not null)
            {
                target = new System.Windows.Shapes.Path
                {
                    Data = System.Windows.Media.Geometry.Parse(item.Path),
                    Width = 1409,
                    Height = 905,
                    Stretch = System.Windows.Media.Stretch.None,
                    Stroke = Brushes.Transparent,
                    StrokeThickness = 28,
                    Fill = Brushes.Transparent,
                    ToolTip = item.Name,
                    Tag = item.Id,
                    Cursor = System.Windows.Input.Cursors.Hand
                };
            }
            else
            {
                target = new System.Windows.Controls.Border
                {
                    Width = item.Width,
                    Height = item.Height,
                    Background = Brushes.Transparent,
                    BorderBrush = Brushes.Transparent,
                    BorderThickness = new Thickness(0),
                    ToolTip = item.Name,
                    Tag = item.Id,
                    Cursor = System.Windows.Input.Cursors.Hand
                };
                System.Windows.Controls.Canvas.SetLeft(target, item.X);
                System.Windows.Controls.Canvas.SetTop(target, item.Y);
            }

            target.MouseLeftButtonDown += EnergyMapObject_MouseLeftButtonDown;
            EnergyMapObjectHitCanvas.Children.Add(target);
        }
    }

    private void EnergyMapObject_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (!_isAdminMode || _energyMapEditMode || sender is not System.Windows.FrameworkElement target ||
            target.Tag is not string id)
            return;

        _selectedEnergyMapObjectId = id;
        if (e.ClickCount == 2)
        {
            var objectName = target.ToolTip?.ToString() ?? id;
            var currentBinding = _energyMapObjectBindings.TryGetValue(id, out var value) ? value : "";
            var dialog = new EnergyMapObjectPropertiesDialog(objectName, currentBinding, this);
            if (dialog.ShowDialog() == true)
            {
                _energyMapObjectBindings[id] = dialog.BindingKey;
                ClientSettings.SaveEnergyMapObjectBindings(_energyMapObjectBindings);
            }
        }
        e.Handled = true;
    }

    private void RenderEnergyMapElements()
    {
        EnergyMapItemsCanvas.Children.Clear();
        _energyMapItems.Clear();

        foreach (var element in _energyMapElements)
        {
            var text = new System.Windows.Controls.TextBlock
            {
                Text = "",
                Foreground = EnergyMapBrush(element.Foreground, Brushes.White),
                FontSize = element.FontSize,
                FontWeight = element.Bold ? FontWeights.Bold : FontWeights.Normal,
                FontFamily = EnergyMapFont(element.FontFamily),
                TextAlignment = TextAlignment.Center,
                TextWrapping = TextWrapping.Wrap,
                VerticalAlignment = VerticalAlignment.Center,
                HorizontalAlignment = HorizontalAlignment.Center
            };

            SetEnergyMapText(text, element);

            // LayoutTransform applies the independent X/Y text scaling during WPF layout,
            // so shrinking an element also shrinks the text before it is arranged.
            text.LayoutTransform = new ScaleTransform(element.TextScaleX, element.TextScaleY);

            var isIndicator = element.Kind == "Indicator";
            var item = new System.Windows.Controls.Border
            {
                Width = Math.Max(12, element.Width),
                Height = Math.Max(12, element.Height),
                Padding = isIndicator ? new Thickness(5, 2, 5, 2) : new Thickness(0),
                Background = EnergyMapBrush(element.Background, Brushes.Transparent),
                BorderBrush = EnergyMapBrush(element.BorderColor, Brushes.Transparent),
                BorderThickness = element.BorderWidth > 0
                    ? new Thickness(element.BorderWidth)
                    : new Thickness(0),
                CornerRadius = new CornerRadius(element.CornerRadius),
                Child = text,
                Tag = element.Id,
                Cursor = _energyMapEditMode ? System.Windows.Input.Cursors.SizeAll : System.Windows.Input.Cursors.Arrow,
                ToolTip = element.Text
            };
            item.MouseLeftButtonDown += EnergyMapItem_MouseLeftButtonDown;
            item.MouseMove += EnergyMapItem_MouseMove;
            item.MouseLeftButtonUp += EnergyMapItem_MouseLeftButtonUp;
            _energyMapItems[element.Id] = item;
            EnergyMapItemsCanvas.Children.Add(item);
            System.Windows.Controls.Canvas.SetLeft(item, Math.Clamp(element.X, 0, 1409 - item.Width));
            System.Windows.Controls.Canvas.SetTop(item, Math.Clamp(element.Y, 0, 905 - item.Height));
        }
        UpdateEnergyMapHitTesting();
        UpdateEnergyMapSelection();
        ApplyStyleOverrides();
    }

    private static FontFamily EnergyMapFont(string value)
    {
        try { return new FontFamily(string.IsNullOrWhiteSpace(value) ? "Arial" : value); }
        catch { return new FontFamily("Arial"); }
    }

    private static Brush EnergyMapBrush(string value, Brush fallback)
    {
        try
        {
            var converted = ColorConverter.ConvertFromString(value);
            return converted is Color color ? new SolidColorBrush(color) : fallback;
        }
        catch { return fallback; }
    }

    private static string GetEnergyMapDisplayText(EnergyMapElement element)
    {
        if (string.IsNullOrEmpty(element.DataSource) || string.IsNullOrEmpty(element.CurrentValue))
            return element.Text;
        return element.Text.Contains("{value}", StringComparison.Ordinal)
            ? element.Text.Replace("{value}", element.CurrentValue, StringComparison.Ordinal)
            : element.CurrentValue;
    }

    private static void SetEnergyMapText(System.Windows.Controls.TextBlock text, EnergyMapElement element)
    {
        text.Inlines.Clear();
        var displayText = GetEnergyMapDisplayText(element);
        if (!string.IsNullOrEmpty(element.AccentText) && displayText.Contains(element.AccentText, StringComparison.Ordinal))
        {
            var accentIndex = displayText.IndexOf(element.AccentText, StringComparison.Ordinal);
            if (accentIndex > 0) text.Inlines.Add(new System.Windows.Documents.Run(displayText[..accentIndex]));
            text.Inlines.Add(new System.Windows.Documents.Run(element.AccentText)
            {
                Foreground = EnergyMapBrush(element.AccentForeground, text.Foreground),
                FontWeight = FontWeights.Bold
            });
            var tail = displayText[(accentIndex + element.AccentText.Length)..];
            if (tail.Length > 0) text.Inlines.Add(new System.Windows.Documents.Run(tail));
        }
        else text.Text = displayText;
    }

    private void EnergyMapEdit_Click(object sender, RoutedEventArgs e)
    {
        _energyMapEditMode = !_energyMapEditMode;
        EnergyMapEditMenuItem.Header = _energyMapEditMode ? "Завершить перенос" : "Перемещать";
        EnergyMapObjectHitCanvas.IsHitTestVisible = _isAdminMode && !_energyMapEditMode;
        UpdateEnergyMapHitTesting();
        UpdateEnergyMapSelection();
        foreach (var pair in _energyMapItems)
        {
            var element = _energyMapElements.FirstOrDefault(candidate => candidate.Id == pair.Key);
            var item = pair.Value;
            item.BorderBrush = _energyMapEditMode
                ? new SolidColorBrush(Color.FromRgb(245, 185, 66))
                : Brushes.Transparent;
            item.BorderThickness = _energyMapEditMode
                ? new Thickness(Math.Max(1.5, element?.BorderWidth ?? 0))
                : new Thickness(0);
            item.Cursor = _energyMapEditMode
                ? System.Windows.Input.Cursors.SizeAll : System.Windows.Input.Cursors.Arrow;
            item.ToolTip = _energyMapEditMode
                ? "Перетащите элемент мышью"
                : item.Child is System.Windows.Controls.TextBlock text ? text.Text : "";
        }
        UpdateEnergyMapSelection();
        if (!_energyMapEditMode) SaveEnergyMapLayout();
    }

    private void EnergyMapElements_Click(object sender, RoutedEventArgs e)
    {
        var editor = new EnergyMapEditorWindow(_energyMapElements, this);
        if (editor.ShowDialog() != true) return;
        _energyMapElements = editor.Elements;
        RenderEnergyMapElements();
        SaveEnergyMapLayout();
    }

    private void UpdateEnergyMapHitTesting()
    {
        EnergyMapItemsCanvas.IsHitTestVisible = _isAdminMode;
        foreach (var pair in _energyMapItems)
        {
            var element = _energyMapElements.FirstOrDefault(candidate => candidate.Id == pair.Key);
            pair.Value.IsHitTestVisible = _isAdminMode && element is not null;
        }
    }

    private void EnergyMapItem_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (!_isAdminMode || sender is not System.Windows.Controls.Border item ||
            item.Tag is not string elementId)
            return;

        var element = _energyMapElements.FirstOrDefault(candidate => candidate.Id == elementId);
        if (element is null) return;

        if (!_energyMapEditMode)
        {
            if (e.ClickCount == 2)
            {
                var dialog = new EnergyMapPropertiesDialog(element, this);
                if (dialog.ShowDialog() == true)
                {
                    if (dialog.DeleteRequested)
                    {
                        _energyMapElements.Remove(element);
                        _selectedEnergyMapObjectId = null;
                    }
                    else
                    {
                        element.Width = dialog.ElementWidth;
                        element.Height = dialog.ElementHeight;
                        element.TextScaleX = dialog.ElementTextScaleX;
                        element.TextScaleY = dialog.ElementTextScaleY;
                        element.FontSize = dialog.ElementFontSize;
                        element.FontFamily = dialog.ElementFontFamily;
                        element.Bold = dialog.ElementBold;
                        element.Foreground = dialog.ElementForeground;
                        element.Background = dialog.ElementBackground;
                        element.BorderColor = dialog.ElementBorderColor;
                        element.BorderWidth = dialog.ElementBorderWidth;
                        element.CornerRadius = dialog.ElementCornerRadius;
                        element.AccentText = dialog.ElementAccentText;
                        element.AccentForeground = dialog.ElementAccentForeground;
                        element.Text = dialog.ElementText;
                        element.DataSource = dialog.DataSource;
                    }

                    RenderEnergyMapElements();
                    SaveEnergyMapLayout();
                }
            }
            else
            {
                _selectedEnergyMapObjectId = element.Id;
                UpdateEnergyMapSelection();
            }

            e.Handled = true;
            return;
        }

        _selectedEnergyMapObjectId = element.Id;
        UpdateEnergyMapSelection();
        _draggedEnergyMapItem = item;
        _energyMapDragOffset = e.GetPosition(item);
        item.CaptureMouse();
        e.Handled = true;
    }

    private void UpdateEnergyMapSelection()
    {
        foreach (var pair in _energyMapItems)
        {
            var selected = pair.Key == _selectedEnergyMapObjectId;
            var element = _energyMapElements.FirstOrDefault(candidate => candidate.Id == pair.Key);
            pair.Value.BorderBrush = selected || _energyMapEditMode
                ? new SolidColorBrush(Color.FromRgb(245, 185, 66))
                : element is not null ? EnergyMapBrush(element.BorderColor, Brushes.Transparent) : Brushes.Transparent;
            pair.Value.BorderThickness = selected || _energyMapEditMode
                ? new Thickness(Math.Max(1.5, element?.BorderWidth ?? 0))
                : element is not null && element.BorderWidth > 0
                    ? new Thickness(element.BorderWidth)
                    : new Thickness(0);
        }
    }

    private void EnergyMapItem_MouseMove(object sender, System.Windows.Input.MouseEventArgs e)
    {
        if (!_energyMapEditMode || _draggedEnergyMapItem is null ||
            !_draggedEnergyMapItem.IsMouseCaptured || e.LeftButton != System.Windows.Input.MouseButtonState.Pressed)
            return;
        var position = e.GetPosition(EnergyMapItemsCanvas);
        var x = Math.Clamp(position.X - _energyMapDragOffset.X, 0, 1409 - _draggedEnergyMapItem.Width);
        var y = Math.Clamp(position.Y - _energyMapDragOffset.Y, 0, 905 - _draggedEnergyMapItem.ActualHeight);
        System.Windows.Controls.Canvas.SetLeft(_draggedEnergyMapItem, x);
        System.Windows.Controls.Canvas.SetTop(_draggedEnergyMapItem, y);
        if (_draggedEnergyMapItem.Tag is string id)
        {
            var model = _energyMapElements.FirstOrDefault(element => element.Id == id);
            if (model is not null) { model.X = x; model.Y = y; }
        }
    }

    private void EnergyMapItem_MouseLeftButtonUp(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (_draggedEnergyMapItem is null) return;
        _draggedEnergyMapItem.ReleaseMouseCapture();
        _draggedEnergyMapItem = null;
        SaveEnergyMapLayout();
    }

    private void SaveEnergyMapLayout()
    {
        ClientSettings.SaveEnergyMapElements(_energyMapElements);
    }

    private void SetEnergyMapValue(string id, string value)
    {
        foreach (var element in _energyMapElements.Where(element => element.DataSource == id))
        {
            element.CurrentValue = value;
            if (_energyMapItems.TryGetValue(element.Id, out var item) &&
                item.Child is System.Windows.Controls.TextBlock text)
                SetEnergyMapText(text, element);
        }
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
        SetDigitalString(1, 1084, 186.4, 5.82);
        SetString(2, "758 Вт", "182,1 В", "4,16 А", "5 631,4 кВт·ч", "ДЕМО");
        SetDigitalString(2, 758, 182.1, 4.16);
        DigitalInverterPower.Text = "1 842 Вт";
        SetEnergyMapValue("InverterPower", "1 842 Вт");
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
            SetDigitalString(id, valid ? power : double.NaN, valid ? voltage : double.NaN, valid ? current : double.NaN);
            SetString(id,
                valid ? Format(power, "N0") + " Вт" : "Нет данных",
                Format(voltage, "N1") + " В",
                Format(current, "N2") + " А",
                Format(energy, "N3") + " кВт·ч",
                valid ? "НА СВЯЗИ" : "НЕТ ДАННЫХ");
        }
        TotalPowerText.Text = Format(totalPower, "N0");
        DigitalInverterPower.Text = Format(totalPower, "N0") + " Вт";
        SetEnergyMapValue("InverterPower", Format(totalPower, "N0") + " Вт");
    }


    private void SetDigitalString(int id, double power, double voltage, double current)
    {
        var powerText = double.IsNaN(power) ? "— Вт" : Format(power, "N0") + " Вт";
        var lineText = double.IsNaN(power) ? "— кВт" : Format(power / 1000.0, "N2") + " кВт";
        if (id == 1)
        {
            DigitalPv1Power.Text = powerText;
            DigitalPv1Voltage.Text = double.IsNaN(voltage) ? "— В" : Format(voltage, "N1") + " В";
            DigitalPv1Current.Text = double.IsNaN(current) ? "— А" : Format(current, "N2") + " А";
            SetEnergyMapValue("Pv1Power", powerText);
            SetEnergyMapValue("Pv1Voltage", double.IsNaN(voltage) ? "— В" : Format(voltage, "N1") + " В");
            SetEnergyMapValue("Pv1Current", double.IsNaN(current) ? "— А" : Format(current, "N2") + " А");
            DigitalPv1LinePower.Text = lineText;
        }
        else
        {
            DigitalPv2Power.Text = powerText;
            DigitalPv2Voltage.Text = double.IsNaN(voltage) ? "— В" : Format(voltage, "N1") + " В";
            DigitalPv2Current.Text = double.IsNaN(current) ? "— А" : Format(current, "N2") + " А";
            SetEnergyMapValue("Pv2Power", powerText);
            SetEnergyMapValue("Pv2Voltage", double.IsNaN(voltage) ? "— В" : Format(voltage, "N1") + " В");
            SetEnergyMapValue("Pv2Current", double.IsNaN(current) ? "— А" : Format(current, "N2") + " А");
            DigitalPv2LinePower.Text = lineText;
        }
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