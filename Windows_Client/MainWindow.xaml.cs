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
    private readonly Dictionary<string, System.Windows.Controls.Border> _energyMapItems = new();
    private Dictionary<string, string> _energyMapObjectBindings = ClientSettings.LoadEnergyMapObjectBindings();
    private List<EnergyMapElement> _energyMapElements = new();
    private bool _energyMapEditMode;
    private System.Windows.Controls.Border? _draggedEnergyMapItem;
    private System.Windows.Point _energyMapDragOffset;
    private string? _selectedEnergyMapObjectId;

    private bool _isAdminMode;

    private string _activeDesign = "EnergyMap";

    public MainWindow()
    {
        InitializeComponent();
        BuildEnergyMapItems();
        UpdateEnergyMapHitTesting();
        UpdateWindowStateButton();
        var version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "—";
        VersionText.Text = $"Solar_PV Desktop {version}";
        AddressBox.Text = ClientSettings.LoadControllerAddress() ?? AddressBox.Text;
        var savedDesign = "EnergyMap";
        Loaded += (_, _) =>
        {
            ApplyTheme();
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
        EnergyMapObjectHitCanvas.IsHitTestVisible = !_energyMapEditMode;
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
            Background = EnergyMapEditorSystemTheme.BrushFor("background|dialog"),
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|dialog")
        };

        var panel = new System.Windows.Controls.StackPanel { Margin = new Thickness(22) };
        panel.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = "РЕЖИМ АДМИНИСТРАТОРА",
            FontSize = 16,
            FontWeight = FontWeights.Bold,
            Margin = new Thickness(0, 0, 0, 16),
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|accent")
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
            Tag = "SystemPrimaryButton",
            Width = 90,
            Height = 32,
            Background = EnergyMapEditorSystemTheme.BrushFor("background|button-primary"),
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|button"),
            FontWeight = FontWeights.SemiBold,
            IsDefault = true
        };
        enterButton.Click += (_, _) => loginWindow.DialogResult = true;

        buttons.Children.Add(cancelButton);
        buttons.Children.Add(enterButton);
        panel.Children.Add(buttons);
        loginWindow.Content = panel;
        loginWindow.Loaded += (_, _) => EnergyMapEditorSystemTheme.Apply(loginWindow);

        return loginWindow.ShowDialog() == true;
    }

    private void About_Click(object sender, RoutedEventArgs e)
    {
        var version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "—";
        EnergyMapEditorSystemTheme.ShowMessage(this, $"Solar_PV Desktop {version}\nКлиент мониторинга солнечной станции\nWindows 10 / 11",
            "О программе", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private void ApplyTheme()
    {
        EnergyMapEditorSystemTheme.Apply(this, EnergyMapDashboard, DigitalDashboard, LegacyDashboard);
        Background = EnergyMapEditorSystemTheme.BrushFor("background|window");
        TitleBarGrid.Background = EnergyMapEditorSystemTheme.BrushFor("background|titlebar");
        MainMenu.Background = EnergyMapEditorSystemTheme.BrushFor("background|menu");
    }

    private void ApplyDesign(string design)
    {
        if (design is not ("Digital" or "EnergyMap" or "Flow" or "Blueprint"))
            design = "Digital";
        _activeDesign = design;

        var accent = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#F5B942"));
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
        var borderBrush = EnergyMapEditorSystemTheme.BrushFor("background|border");

        var cornerRadius = design switch
        {
            "Flow" => new CornerRadius(1),
            "Blueprint" => new CornerRadius(0),
            _ => new CornerRadius(4)
        };

        void StyleCards(DependencyObject parent)
        {
            if (ReferenceEquals(parent, EnergyMapDashboard) ||
                ReferenceEquals(parent, DigitalDashboard) ||
                ReferenceEquals(parent, LegacyDashboard) ||
                ReferenceEquals(parent, EnergyMapObjectHitCanvas) ||
                ReferenceEquals(parent, EnergyMapItemsCanvas))
                return;

            for (var i = 0; i < System.Windows.Media.VisualTreeHelper.GetChildrenCount(parent); i++)
            {
                var child = System.Windows.Media.VisualTreeHelper.GetChild(parent, i);
                if (child is System.Windows.Controls.Border border)
                {
                    border.CornerRadius = cornerRadius;
                    border.BorderBrush = borderBrush;
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
                EnergyMapEditorSystemTheme.BrushFor("background|window"),
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
            Background = EnergyMapEditorSystemTheme.BrushFor("background|window");
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
        if (_energyMapEditMode || sender is not System.Windows.FrameworkElement target ||
            target.Tag is not string id)
            return;

        if (id == "house")
        {
            new HouseStatisticsWindow { Owner = this }.Show();
            e.Handled = true;
            return;
        }

        if (!_isAdminMode) return;
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
        EnergyMapObjectHitCanvas.IsHitTestVisible = !_energyMapEditMode;
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
        // Прозрачные цели объектов доступны в рабочем режиме; при переносе отключаются.
        EnergyMapObjectHitCanvas.IsHitTestVisible = !_energyMapEditMode;
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
            var item = pair.Value;

            if (_energyMapEditMode)
            {
                // В режиме перемещения оставляем заметную рамку режима редактирования.
                item.BorderBrush = new SolidColorBrush(Color.FromRgb(245, 185, 66));
                item.BorderThickness = new Thickness(Math.Max(1.5, element?.BorderWidth ?? 0));
                item.Effect = null;
            }
            else
            {
                // Выделение больше не подменяет пользовательские цвет и толщину рамки.
                item.BorderBrush = element is not null
                    ? EnergyMapBrush(element.BorderColor, Brushes.Transparent)
                    : Brushes.Transparent;
                item.BorderThickness = element is not null && element.BorderWidth > 0
                    ? new Thickness(element.BorderWidth)
                    : new Thickness(0);
                item.Effect = selected
                    ? new System.Windows.Media.Effects.DropShadowEffect
                    {
                        Color = Color.FromRgb(245, 185, 66),
                        BlurRadius = 10,
                        ShadowDepth = 0,
                        Opacity = 0.9
                    }
                    : null;
            }
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
            EnergyMapEditorSystemTheme.ShowMessage(this, "Введите адрес контроллера, например http://192.168.4.1", "Solar_PV",
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
            EnergyMapEditorSystemTheme.ShowMessage(this, "Не удалось открыть журнал связи: " + ex.Message,
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

/// <summary>
/// Окно статистики домашнего энергопотребления и выходного напряжения инвертора.
/// Пока использует встроенный виртуальный набор измерений.
/// </summary>
internal sealed class HouseStatisticsWindow : Window
{
    private readonly System.Windows.Controls.ComboBox _period = new();
    private readonly System.Windows.Controls.ComboBox _metric = new();
    private readonly System.Windows.Controls.ComboBox _style = new();
    private readonly System.Windows.Controls.DatePicker _from = new();
    private readonly System.Windows.Controls.DatePicker _to = new();
    private readonly System.Windows.Controls.TextBlock _powerValue = new();
    private readonly System.Windows.Controls.TextBlock _energyValue = new();
    private readonly System.Windows.Controls.TextBlock _voltageValue = new();
    private readonly System.Windows.Controls.TextBlock _rangeValue = new();
    private readonly System.Windows.Controls.Canvas _chart = new();
    private readonly System.Windows.Controls.TextBlock _chartTitle = new();
    private readonly System.Windows.Controls.TextBlock _chartFooter = new();
    private DateTime _start;
    private DateTime _end;

    public HouseStatisticsWindow()
    {
        Title = "Solar_PV — Статистика дома";
        Width = 1060;
        Height = 720;
        MinWidth = 780;
        MinHeight = 560;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = EnergyMapEditorSystemTheme.BrushFor("background|window");
        Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary");
        FontFamily = new System.Windows.Media.FontFamily("Segoe UI");

        var root = new System.Windows.Controls.Grid { Margin = new Thickness(22) };
        root.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = GridLength.Auto });
        Content = root;

        var heading = new System.Windows.Controls.StackPanel { Margin = new Thickness(0, 0, 0, 18) };
        heading.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = "ЭНЕРГОПОТРЕБЛЕНИЕ ДОМА",
            FontSize = 20, FontWeight = FontWeights.SemiBold,
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary")
        });
        heading.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = "Текущая нагрузка и история выходного напряжения инвертора",
            FontSize = 12, Margin = new Thickness(0, 4, 0, 0),
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary")
        });
        root.Children.Add(heading);

        var controls = new System.Windows.Controls.WrapPanel { Margin = new Thickness(0, 0, 0, 16), VerticalAlignment = VerticalAlignment.Center };
        AddControl(controls, "Период", _period, new[] { "Сутки", "Неделя", "Месяц", "Год", "Произвольный" }, 105);
        AddControl(controls, "Показатель", _metric, new[] { "Потребление, кВт·ч", "Напряжение, В" }, 175);
        AddControl(controls, "Вид графика", _style, new[] { "Линейный", "Столбчатый", "Область", "Ступенчатый", "Мощность + напряжение" }, 190);
        AddDateControl(controls, "С", _from);
        AddDateControl(controls, "По", _to);
        System.Windows.Controls.Grid.SetRow(controls, 1);
        root.Children.Add(controls);

        var contentGrid = new System.Windows.Controls.Grid();
        contentGrid.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = GridLength.Auto });
        contentGrid.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        contentGrid.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = GridLength.Auto });
        System.Windows.Controls.Grid.SetRow(contentGrid, 2);
        root.Children.Add(contentGrid);

        var cards = new System.Windows.Controls.Grid { Margin = new Thickness(0, 0, 0, 14) };
        for (int i = 0; i < 4; i++) cards.ColumnDefinitions.Add(new System.Windows.Controls.ColumnDefinition());
        AddCard(cards, 0, "ТЕКУЩАЯ МОЩНОСТЬ", _powerValue, "кВт");
        AddCard(cards, 1, "ПОТРЕБЛЕНИЕ ЗА ПЕРИОД", _energyValue, "кВт·ч");
        AddCard(cards, 2, "НАПРЯЖЕНИЕ СЕТИ", _voltageValue, "В");
        AddCard(cards, 3, "МИНИМУМ / МАКСИМУМ", _rangeValue, "В");
        contentGrid.Children.Add(cards);

        var chartBorder = new System.Windows.Controls.Border
        {
            Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(17, 34, 52)),
            BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(48, 68, 94)),
            BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(10), Padding = new Thickness(12)
        };
        var chartGrid = new System.Windows.Controls.Grid();
        chartGrid.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = GridLength.Auto });
        chartGrid.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        chartGrid.RowDefinitions.Add(new System.Windows.Controls.RowDefinition { Height = GridLength.Auto });
        _chartTitle.Text = "История потребления";
        _chartTitle.FontSize = 14;
        _chartTitle.FontWeight = FontWeights.SemiBold;
        _chartTitle.Margin = new Thickness(4, 0, 0, 8);
        chartGrid.Children.Add(_chartTitle);
        _chart.Background = System.Windows.Media.Brushes.Transparent;
        _chart.SizeChanged += (_, _) => DrawChart();
        System.Windows.Controls.Grid.SetRow(_chart, 1);
        chartGrid.Children.Add(_chart);
        _chartFooter.Text = "Значения по выбранному периоду";
        _chartFooter.Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary");
        _chartFooter.FontSize = 11;
        _chartFooter.Margin = new Thickness(4, 8, 0, 0);
        System.Windows.Controls.Grid.SetRow(_chartFooter, 2);
        chartGrid.Children.Add(_chartFooter);
        chartBorder.Child = chartGrid;
        System.Windows.Controls.Grid.SetRow(chartBorder, 1);
        contentGrid.Children.Add(chartBorder);

        var bottom = new System.Windows.Controls.DockPanel { Margin = new Thickness(0, 12, 0, 0) };
        var close = new System.Windows.Controls.Button
        {
            Content = "Закрыть", Width = 110, Height = 34, Padding = new Thickness(12, 5, 12, 5),
            Background = EnergyMapEditorSystemTheme.BrushFor("background|button"),
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary"), BorderThickness = new Thickness(0),
            HorizontalAlignment = HorizontalAlignment.Right
        };
        close.Click += (_, _) => Close();
        System.Windows.Controls.DockPanel.SetDock(close, Dock.Right);
        bottom.Children.Add(close);
        System.Windows.Controls.Grid.SetRow(bottom, 3);
        root.Children.Add(bottom);

        _period.SelectedIndex = 2;
        _metric.SelectedIndex = 0;
        _style.SelectedIndex = 0;
        var today = DateTime.Today;
        _from.SelectedDate = new DateTime(today.Year, today.Month, 1);
        _to.SelectedDate = today;
        _period.SelectionChanged += (_, _) => ApplyPeriod();
        _metric.SelectionChanged += (_, _) => RefreshStatistics();
        _style.SelectionChanged += (_, _) => DrawChart();
        _from.SelectedDateChanged += (_, _) => RefreshStatistics();
        _to.SelectedDateChanged += (_, _) => RefreshStatistics();
        Loaded += (_, _) => EnergyMapEditorSystemTheme.Apply(this, chartBorder);
        UpdateDateVisibility();
        RefreshStatistics();
    }



    private static void AddControl(System.Windows.Controls.WrapPanel panel, string label,
        System.Windows.Controls.ComboBox combo, string[] options, double width)
    {
        var group = new System.Windows.Controls.StackPanel { Margin = new Thickness(0, 0, 12, 8) };
        group.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = label, FontSize = 11,
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary"),
            Background = EnergyMapEditorSystemTheme.BrushFor("background|surface"),
            Margin = new Thickness(0, 0, 0, 4)
        });
        combo.Width = width;
        combo.Height = 30;
        combo.Background = EnergyMapEditorSystemTheme.BrushFor("background|input");
        combo.Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary");
        combo.BorderBrush = EnergyMapEditorSystemTheme.BrushFor("background|border");
        combo.ItemsSource = options;
        combo.SelectedIndex = 0;
        combo.Padding = new Thickness(6, 3, 6, 3);
        group.Children.Add(combo);
        panel.Children.Add(group);
    }

    private static void AddDateControl(System.Windows.Controls.WrapPanel panel, string label,
        System.Windows.Controls.DatePicker picker)
    {
        var group = new System.Windows.Controls.StackPanel { Margin = new Thickness(0, 0, 10, 8) };
        group.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = label, FontSize = 11,
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary"),
            Background = EnergyMapEditorSystemTheme.BrushFor("background|surface"),
            Margin = new Thickness(0, 0, 0, 4)
        });
        picker.Width = 125;
        picker.Height = 30;
        picker.Background = EnergyMapEditorSystemTheme.BrushFor("background|input");
        picker.Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary");
        picker.Visibility = Visibility.Collapsed;
        group.Children.Add(picker);
        panel.Children.Add(group);
    }

    private static void AddCard(System.Windows.Controls.Grid grid, int column, string title,
        System.Windows.Controls.TextBlock value, string unit)
    {
        var card = new System.Windows.Controls.Border
        {
            Background = EnergyMapEditorSystemTheme.BrushFor("background|surface"),
            BorderBrush = EnergyMapEditorSystemTheme.BrushFor("background|border"),
            BorderThickness = new Thickness(1), CornerRadius = new CornerRadius(8),
            Padding = new Thickness(12), Margin = new Thickness(0, 0, column == 3 ? 0 : 10, 0)
        };
        var stack = new System.Windows.Controls.StackPanel();
        stack.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = title, FontSize = 10, FontWeight = FontWeights.Bold,
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary")
        });
        value.Text = "—";
        value.FontSize = 23;
        value.FontWeight = FontWeights.SemiBold;
        value.Margin = new Thickness(0, 7, 0, 0);
        stack.Children.Add(value);
        stack.Children.Add(new System.Windows.Controls.TextBlock
        {
            Text = unit, FontSize = 10,
            Foreground = EnergyMapEditorSystemTheme.BrushFor("text|primary")
        });
        card.Child = stack;
        System.Windows.Controls.Grid.SetColumn(card, column);
        grid.Children.Add(card);
    }

    private void ApplyPeriod()
    {
        var today = DateTime.Today;
        switch (_period.SelectedIndex)
        {
            case 0: _start = today; _end = today; break;
            case 1: _start = today.AddDays(-6); _end = today; break;
            case 2: _start = new DateTime(today.Year, today.Month, 1); _end = today; break;
            case 3: _start = new DateTime(today.Year, 1, 1); _end = today; break;
            default:
                if (_from.SelectedDate is DateTime from) _start = from.Date;
                if (_to.SelectedDate is DateTime to) _end = to.Date;
                break;
        }
        UpdateDateVisibility();
        if (_period.SelectedIndex != 4)
        {
            _from.SelectedDate = _start;
            _to.SelectedDate = _end;
        }
        RefreshStatistics();
    }

    private void UpdateDateVisibility()
    {
        var custom = _period.SelectedIndex == 4;
        _from.Visibility = custom ? Visibility.Visible : Visibility.Collapsed;
        _to.Visibility = custom ? Visibility.Visible : Visibility.Collapsed;
        if (custom && _from.SelectedDate is DateTime from && _to.SelectedDate is DateTime to)
        {
            _start = from.Date;
            _end = to.Date;
        }
    }

    private void RefreshStatistics()
    {
        if (_period.SelectedIndex == 4)
        {
            _start = (_from.SelectedDate ?? DateTime.Today).Date;
            _end = (_to.SelectedDate ?? DateTime.Today).Date;
            if (_end < _start) _end = _start;
        }
        else if (_period.SelectedIndex >= 0)
        {
            var today = DateTime.Today;
            if (_period.SelectedIndex == 0) { _start = today; _end = today; }
            else if (_period.SelectedIndex == 1) { _start = today.AddDays(-6); _end = today; }
            else if (_period.SelectedIndex == 2) { _start = new DateTime(today.Year, today.Month, 1); _end = today; }
            else { _start = new DateTime(today.Year, 1, 1); _end = today; }
        }

        var samples = CreateSamples();
        var total = samples.Sum(x => x.Energy);
        var min = samples.Min(x => x.Voltage);
        var max = samples.Max(x => x.Voltage);
        var last = samples[^1];
        _powerValue.Text = last.Power.ToString("N2", CultureInfo.GetCultureInfo("ru-RU"));
        _energyValue.Text = total.ToString("N2", CultureInfo.GetCultureInfo("ru-RU"));
        _voltageValue.Text = last.Voltage.ToString("N1", CultureInfo.GetCultureInfo("ru-RU"));
        _rangeValue.Text = min.ToString("N1", CultureInfo.GetCultureInfo("ru-RU")) + " / " + max.ToString("N1", CultureInfo.GetCultureInfo("ru-RU"));
        _chartTitle.Text = _style.SelectedIndex == 4
            ? "Мощность дома и выходное напряжение инвертора"
            : _metric.SelectedIndex == 1 ? "История выходного напряжения инвертора" : "Потребление электроэнергии";
        _chartFooter.Text = $"{_start:dd.MM.yyyy} — {_end:dd.MM.yyyy}   ·   Минимум: {min:N1} В   ·   Максимум: {max:N1} В";
        DrawChart();
    }

    private List<(double Energy, double Power, double Voltage)> CreateSamples()
    {
        var days = Math.Max(1, (_end - _start).Days + 1);
        var count = _period.SelectedIndex == 0 ? 24 :
            _period.SelectedIndex == 3 ? 12 : Math.Clamp(days, 7, 240);
        var result = new List<(double Energy, double Power, double Voltage)>(count);
        for (int i = 0; i < count; i++)
        {
            var phase = i * 0.73 + _start.DayOfYear * 0.17;
            var power = 0.55 + 1.35 * Math.Max(0, Math.Sin(phase)) + 0.25 * Math.Sin(phase * 2.1);
            power = Math.Max(0.18, power);
            var energy = _period.SelectedIndex == 0 ? power * 0.72 : power * (i % 5 == 0 ? 1.8 : 1.15);
            var voltage = 230 + 5.2 * Math.Sin(phase * 0.41) + 2.1 * Math.Sin(phase * 1.7);
            if (i % 19 == 7) voltage -= 8.5;
            if (i % 23 == 11) voltage += 4.2;
            result.Add((energy, power, voltage));
        }
        return result;
    }

    private void DrawChart()
    {
        if (_chart.ActualWidth < 80 || _chart.ActualHeight < 80) return;
        _chart.Children.Clear();
        var samples = CreateSamples();
        double width = _chart.ActualWidth, height = _chart.ActualHeight;
        const double left = 48, right = 18, top = 16, bottom = 28;
        double plotWidth = width - left - right, plotHeight = height - top - bottom;
        if (plotWidth <= 0 || plotHeight <= 0) return;

        var gridBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(48, 68, 94));
        for (int i = 0; i <= 4; i++)
        {
            double y = top + plotHeight * i / 4;
            _chart.Children.Add(new System.Windows.Shapes.Line { X1 = left, X2 = width - right, Y1 = y, Y2 = y, Stroke = gridBrush, StrokeThickness = 1, StrokeDashArray = new System.Windows.Media.DoubleCollection { 2, 3 } });
        }

        bool voltage = _metric.SelectedIndex == 1;
        bool mixed = _style.SelectedIndex == 4;
        var values = mixed
            ? samples.Select(x => x.Power).ToArray()
            : voltage ? samples.Select(x => x.Voltage).ToArray() : samples.Select(x => x.Energy).ToArray();
        double min = values.Min(), max = values.Max();
        if (Math.Abs(max - min) < 0.001) max = min + 1;
        double X(int i) => left + (samples.Count == 1 ? 0 : plotWidth * i / (samples.Count - 1));
        double Y(double value, double low, double high) => top + plotHeight - (value - low) / (high - low) * plotHeight;
        var primary = voltage
            ? new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(99, 185, 242))
            : new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(99, 214, 162));

        if (_style.SelectedIndex == 1 && !mixed)
        {
            double barWidth = Math.Max(2, plotWidth / samples.Count * 0.62);
            for (int i = 0; i < samples.Count; i++)
            {
                double y = Y(values[i], min, max);
                _chart.Children.Add(new System.Windows.Shapes.Rectangle
                {
                    Width = barWidth, Height = Math.Max(2, top + plotHeight - y),
                    Fill = primary, RadiusX = 2, RadiusY = 2,
                    Opacity = 0.82
                });
                System.Windows.Controls.Canvas.SetLeft(_chart.Children[^1], X(i) - barWidth / 2);
                System.Windows.Controls.Canvas.SetTop(_chart.Children[^1], y);
            }
        }
        else
        {
            var points = new System.Windows.Media.PointCollection();
            for (int i = 0; i < samples.Count; i++) points.Add(new System.Windows.Point(X(i), Y(values[i], min, max)));
            if (_style.SelectedIndex == 2 && !mixed)
            {
                var areaPoints = new System.Windows.Media.PointCollection { new System.Windows.Point(X(0), top + plotHeight) };
                foreach (var point in points) areaPoints.Add(point);
                areaPoints.Add(new System.Windows.Point(X(samples.Count - 1), top + plotHeight));
                _chart.Children.Add(new System.Windows.Shapes.Polygon { Points = areaPoints, Fill = primary, Opacity = 0.22, StrokeThickness = 0 });
            }
            if (_style.SelectedIndex == 3 && !mixed)
            {
                var stepPoints = new System.Windows.Media.PointCollection();
                for (int i = 0; i < points.Count; i++)
                {
                    if (i > 0) stepPoints.Add(new System.Windows.Point(points[i].X, points[i - 1].Y));
                    stepPoints.Add(points[i]);
                }
                points = stepPoints;
            }
            _chart.Children.Add(new System.Windows.Shapes.Polyline { Points = points, Stroke = primary, StrokeThickness = 2.4, StrokeLineJoin = System.Windows.Media.PenLineJoin.Round });
        }

        if (mixed)
        {
            var voltageValues = samples.Select(x => x.Voltage).ToArray();
            double vMin = voltageValues.Min(), vMax = voltageValues.Max();
            if (vMax - vMin < 0.001) vMax = vMin + 1;
            var voltagePoints = new System.Windows.Media.PointCollection();
            for (int i = 0; i < samples.Count; i++) voltagePoints.Add(new System.Windows.Point(X(i), Y(voltageValues[i], vMin, vMax)));
            _chart.Children.Add(new System.Windows.Shapes.Polyline
            {
                Points = voltagePoints,
                Stroke = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(245, 185, 66)),
                StrokeThickness = 2, StrokeDashArray = new System.Windows.Media.DoubleCollection { 5, 3 }
            });
        }

        var topLabel = new System.Windows.Controls.TextBlock
        {
            Text = max.ToString("N1", CultureInfo.GetCultureInfo("ru-RU")),
            Foreground = System.Windows.Media.Brushes.White, FontSize = 10
        };
        _chart.Children.Add(topLabel);
        System.Windows.Controls.Canvas.SetLeft(topLabel, 2);
        System.Windows.Controls.Canvas.SetTop(topLabel, top - 6);
        var bottomLabel = new System.Windows.Controls.TextBlock
        {
            Text = min.ToString("N1", CultureInfo.GetCultureInfo("ru-RU")),
            Foreground = System.Windows.Media.Brushes.White, FontSize = 10
        };
        _chart.Children.Add(bottomLabel);
        System.Windows.Controls.Canvas.SetLeft(bottomLabel, 2);
        System.Windows.Controls.Canvas.SetTop(bottomLabel, top + plotHeight - 6);
        var startLabel = new System.Windows.Controls.TextBlock { Text = _start.ToString("dd.MM"), Foreground = System.Windows.Media.Brushes.LightGray, FontSize = 10 };
        _chart.Children.Add(startLabel);
        System.Windows.Controls.Canvas.SetLeft(startLabel, left);
        System.Windows.Controls.Canvas.SetTop(startLabel, height - 22);
        var endLabel = new System.Windows.Controls.TextBlock { Text = _end.ToString("dd.MM"), Foreground = System.Windows.Media.Brushes.LightGray, FontSize = 10 };
        _chart.Children.Add(endLabel);
        System.Windows.Controls.Canvas.SetLeft(endLabel, width - right - 40);
        System.Windows.Controls.Canvas.SetTop(endLabel, height - 22);
    }
}

