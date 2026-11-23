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
    private List<EnergyMapElement> _energyMapElements = new();
    private bool _energyMapEditMode;
    private System.Windows.Controls.Border? _draggedEnergyMapItem;
    private System.Windows.Point _energyMapDragOffset;
    private string? _selectedEnergyMapObjectId;

    private bool _isAdminMode;

    public MainWindow()
    {
        InitializeComponent();
        BuildEnergyMapItems();
        UpdateWindowStateButton();
        var version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "—";
        VersionText.Text = $"Solar_PV Desktop {version}";
        AddressBox.Text = ClientSettings.LoadControllerAddress() ?? AddressBox.Text;
        var savedTheme = ClientSettings.LoadTheme() ?? "Dark";
        var savedDesign = ClientSettings.LoadDesign() ?? "Digital";
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
            EnergyMapEditButton.Content = "Перемещать";
            RenderEnergyMapElements();
            SaveEnergyMapLayout();
        }
        EnergyMapEditButton.Visibility = enabled ? Visibility.Visible : Visibility.Collapsed;
        EnergyMapElementsButton.Visibility = enabled ? Visibility.Visible : Visibility.Collapsed;
        EnergyMapObjectHitCanvas.IsHitTestVisible = enabled && !_energyMapEditMode;
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
            Background = new SolidColorBrush(Color.FromRgb(18, 30, 48)),
            Foreground = Brushes.White
        };

        var panel = new System.Windows.Controls.StackPanel { Margin = new Thickness(22) };
        panel.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = "РЕЖИМ АДМИНИСТРАТОРА",
            FontSize = 16,
            FontWeight = FontWeights.Bold,
            Margin = new Thickness(0, 0, 0, 16),
            Foreground = new SolidColorBrush(Color.FromRgb(245, 185, 66))
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
            Background = new SolidColorBrush(Color.FromRgb(245, 185, 66)),
            Foreground = new SolidColorBrush(Color.FromRgb(23, 32, 45)),
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
        ApplyDesign(design);
    }

    private void ApplyTheme(string theme)
    {
        var palette = theme switch
        {
            "Light" => new[] { "#F2F2F2", "#FFFFFF", "#252525", "#666666", "#555555", "#E8EEF7", "#D6D6D6", "#E5E5E5" },
            "Office" => new[] { "#DCE6F1", "#F3F6FA", "#1F3864", "#526780", "#0078D4", "#E8EEF7", "#B4C7E7", "#C9D9EC" },
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
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush(palette[7])));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush(palette[2])));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.MenuItem.HeaderTemplateProperty,
            (DataTemplate)FindResource("MenuItemHeaderTemplate")));
        menuItemStyle.Setters.Add(new Setter(System.Windows.Controls.Control.PaddingProperty, new Thickness(10, 6, 10, 6)));
        var highlighted = new Trigger { Property = System.Windows.Controls.MenuItem.IsHighlightedProperty, Value = true };
        highlighted.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush("#DCE6F2")));
        highlighted.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush("#17202D")));
        menuItemStyle.Triggers.Add(highlighted);
        var hovered = new Trigger { Property = System.Windows.UIElement.IsMouseOverProperty, Value = true };
        hovered.Setters.Add(new Setter(System.Windows.Controls.Control.BackgroundProperty, PaletteBrush("#DCE6F2")));
        hovered.Setters.Add(new Setter(System.Windows.Controls.Control.ForegroundProperty, PaletteBrush("#17202D")));
        menuItemStyle.Triggers.Add(hovered);
        MainMenu.Resources[typeof(System.Windows.Controls.MenuItem)] = menuItemStyle;
        Application.Current.Resources[typeof(System.Windows.Controls.MenuItem)] = menuItemStyle;
        MainMenu.ItemContainerStyle = menuItemStyle;

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
            for (var i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
            {
                var child = System.Windows.Media.VisualTreeHelper.GetChild(parent, i);
                if (child is System.Windows.Controls.Control control)
                {
                    if (control.Background is SolidColorBrush bg) control.Background = ConvertBrush(bg);
                    if (control.Foreground is SolidColorBrush fg) control.Foreground = ConvertBrush(fg);
                }
                if (child is System.Windows.Controls.TextBlock textBlock && textBlock.Foreground is SolidColorBrush textBrush)
                    textBlock.Foreground = ConvertBrush(textBrush);
                if (child is System.Windows.Controls.Border border && border.Background is SolidColorBrush borderBg)
                    border.Background = ConvertBrush(borderBg);
                Visit(child);
            }
        }
        Visit(this);

        var topMetricsTextColor = palette[2];
        void ApplyTopMetricsTextColor(DependencyObject parent)
        {
            if (parent is System.Windows.Controls.TextBlock textBlock)
                textBlock.Foreground = PaletteBrush(topMetricsTextColor);
            for (var i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
                ApplyTopMetricsTextColor(System.Windows.Media.VisualTreeHelper.GetChild(parent, i));
        }
        ApplyTopMetricsTextColor(TopMetricsGrid);

        _activePalette = palette;
        ClientSettings.SaveTheme(theme);
        ApplyDesign(ClientSettings.LoadDesign() ?? "Digital");
    }

    private void ApplyDesign(string design)
    {
        if (design is not ("Digital" or "Analog" or "EnergyMap" or "Flow" or "Blueprint"))
            design = "Digital";

        var palette = _activePalette ?? new[] { "#0B1220", "#121E30", "#E8EEF7", "#8FA2BA", "#F5B942", "#E8EEF7", "#30445E", "#17263A" };
        DigitalDashboard.Visibility = design == "Digital" ? Visibility.Visible : Visibility.Collapsed;
        AnalogDashboard.Visibility = design == "Analog" ? Visibility.Visible : Visibility.Collapsed;
        EnergyMapDashboard.Visibility = design == "EnergyMap" ? Visibility.Visible : Visibility.Collapsed;
        if (design is "Analog" or "EnergyMap")
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
        LegacyDashboard.Visibility = design is "Digital" or "Analog" or "EnergyMap" ? Visibility.Collapsed : Visibility.Visible;
        var accent = new SolidColorBrush((Color)ColorConverter.ConvertFromString(palette[4]));
        var borderBrush = new SolidColorBrush((Color)ColorConverter.ConvertFromString(palette[6]));

        var cornerRadius = design switch
        {
            "Analog" => new CornerRadius(16),
            "Flow" => new CornerRadius(1),
            "Blueprint" => new CornerRadius(0),
            _ => new CornerRadius(4)
        };

        void StyleCards(DependencyObject parent)
        {
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
            "Analog" => new FontFamily("Georgia"),
            "Blueprint" => new FontFamily("Consolas"),
            _ => new FontFamily("Segoe UI")
        };
        TotalPowerText.FontFamily = valueFont;
        TotalEnergyText.FontFamily = valueFont;
        String1Power.FontFamily = valueFont;
        String2Power.FontFamily = valueFont;
        TotalPowerText.FontSize = design == "Analog" ? 36 : 34;
        TotalEnergyText.FontSize = design == "Analog" ? 36 : 34;
        String1Power.FontSize = design == "Analog" ? 31 : 29;
        String2Power.FontSize = design == "Analog" ? 31 : 29;

        TotalPowerText.Foreground = accent;
        String1Power.Foreground = accent;
        String2Power.Foreground = accent;

        ClientSettings.SaveDesign(design);
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

            var isIndicator = element.Kind == "Indicator";
            var item = new System.Windows.Controls.Border
            {
                Width = Math.Max(12, element.Width),
                Height = Math.Max(12, element.Height),
                Padding = isIndicator ? new Thickness(5, 2, 5, 2) : new Thickness(0),
                Background = isIndicator ? EnergyMapBrush(element.Background, Brushes.Transparent) : Brushes.Transparent,
                BorderBrush = _energyMapEditMode
                    ? new SolidColorBrush(Color.FromRgb(245, 185, 66))
                    : EnergyMapBrush(element.BorderColor, Brushes.Transparent),
                BorderThickness = _energyMapEditMode
                    ? new Thickness(Math.Max(1.5, element.BorderWidth))
                    : new Thickness(isIndicator ? element.BorderWidth : 0),
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
        EnergyMapEditButton.Content = _energyMapEditMode ? "Завершить перенос" : "Перемещать";
        EnergyMapEditButton.Background = new SolidColorBrush(_energyMapEditMode
            ? Color.FromRgb(99, 214, 162) : Color.FromRgb(245, 185, 66));
        EnergyMapObjectHitCanvas.IsHitTestVisible = _isAdminMode && !_energyMapEditMode;
        EnergyMapItemsCanvas.IsHitTestVisible = _energyMapEditMode;
        foreach (var pair in _energyMapItems)
        {
            var element = _energyMapElements.FirstOrDefault(candidate => candidate.Id == pair.Key);
            var item = pair.Value;
            item.BorderBrush = _energyMapEditMode
                ? new SolidColorBrush(Color.FromRgb(245, 185, 66))
                : EnergyMapBrush(element?.BorderColor ?? "#00000000", Brushes.Transparent);
            item.BorderThickness = _energyMapEditMode
                ? new Thickness(Math.Max(1.5, element?.BorderWidth ?? 0))
                : new Thickness(element?.Kind == "Indicator" ? element.BorderWidth : 0);
            item.Cursor = _energyMapEditMode
                ? System.Windows.Input.Cursors.SizeAll : System.Windows.Input.Cursors.Arrow;
            item.ToolTip = _energyMapEditMode
                ? "Перетащите элемент мышью"
                : item.Child is System.Windows.Controls.TextBlock text ? text.Text : "";
        }
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

    private void EnergyMapItem_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (!_energyMapEditMode || sender is not System.Windows.Controls.Border item) return;
        _draggedEnergyMapItem = item;
        _energyMapDragOffset = e.GetPosition(item);
        item.CaptureMouse();
        e.Handled = true;
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
        AnalogInverterPower.Text = "1 842 Вт";
        SetEnergyMapValue("InverterPower", "1 842 Вт");
        AnalogInverterEnergy.Text = "12 846,0 кВт·ч";
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
        AnalogInverterEnergy.Text = TotalEnergyText.Text + " кВт·ч";

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
        AnalogInverterPower.Text = Format(totalPower, "N0") + " Вт";
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
            AnalogPv1Power.Text = powerText;
            AnalogPv1Voltage.Text = double.IsNaN(voltage) ? "— В" : Format(voltage, "N1") + " В";
            AnalogPv1Current.Text = double.IsNaN(current) ? "— А" : Format(current, "N2") + " А";
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
            AnalogPv2Power.Text = powerText;
            AnalogPv2Voltage.Text = double.IsNaN(voltage) ? "— В" : Format(voltage, "N1") + " В";
            AnalogPv2Current.Text = double.IsNaN(current) ? "— А" : Format(current, "N2") + " А";
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
            AnalogPv1Energy.Text = "Энергия: " + energy;
        }
        else
        {
            String2Power.Text = power; String2Voltage.Text = voltage;
            String2Current.Text = current; String2Energy.Text = energy; String2State.Text = state;
            AnalogPv2Energy.Text = "Энергия: " + energy;
        }
    }
}