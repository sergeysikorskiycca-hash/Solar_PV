using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Xml.Linq;

namespace SolarPV.Client;

internal sealed class EnergyMapElement
{
    public string Id { get; set; } = Guid.NewGuid().ToString("N");
    public string Kind { get; set; } = "Text";
    public string Text { get; set; } = "";
    public double X { get; set; }
    public double Y { get; set; }
    public double Width { get; set; } = 160;
    public double Height { get; set; } = 42;
    public double FontSize { get; set; } = 22;
    public double TextScaleX { get; set; } = 1;
    public double TextScaleY { get; set; } = 1;
    public string FontFamily { get; set; } = "Arial";
    public string Foreground { get; set; } = "#FFFFFFFF";
    public string Background { get; set; } = "#00000000";
    public string BorderColor { get; set; } = "#00000000";
    public double BorderWidth { get; set; }
    public double CornerRadius { get; set; }
    public bool Bold { get; set; }
    public string DataSource { get; set; } = "";
    public string CurrentValue { get; set; } = "";
    public string AccentText { get; set; } = "";
    public string AccentForeground { get; set; } = "#FFFFFFFF";

    public EnergyMapElement Copy() => (EnergyMapElement)MemberwiseClone();
}

internal static class EnergyMapSvgDocument
{
    private static readonly XNamespace SvgNs = "http://www.w3.org/2000/svg";

    public static List<EnergyMapElement> CreateDefaults(string source, out string backgroundSvg)
    {
        var document = XDocument.Parse(source, LoadOptions.PreserveWhitespace);
        var root = document.Root ?? throw new InvalidDataException("SVG document has no root element.");
        var elements = new List<EnergyMapElement>();
        var index = 0;

        foreach (var textNode in root.Descendants(SvgNs + "text").ToList())
        {
            var parent = textNode.Parent;
            var rect = parent?.Elements(SvgNs + "rect").FirstOrDefault();
            var transform = (string?)parent?.Attribute("transform") ?? "";
            var isIndicator = rect is not null && transform.Contains("scale(1.5)", StringComparison.Ordinal);

            var text = textNode.Value.Trim();
            if (string.IsNullOrWhiteSpace(text))
            {
                textNode.Remove();
                continue;
            }

            var fontSize = ReadNumber(textNode.Attribute("font-size"), 22);
            var fill = (string?)textNode.Attribute("fill") ?? "#FFFFFF";
            var accentNode = textNode.Descendants(SvgNs + "tspan").FirstOrDefault();
            var accentText = accentNode?.Value.Trim() ?? "";
            var accentFill = (string?)accentNode?.Attribute("fill") ?? fill;
            var weight = (string?)textNode.Attribute("font-weight") ?? "";
            var fontFamily = (string?)textNode.Attribute("font-family") ??
                (string?)textNode.Ancestors().FirstOrDefault(node => node.Attribute("font-family") is not null)?.Attribute("font-family") ?? "Arial";
            fontFamily = fontFamily.Split(',')[0].Trim().Trim('\'', '"');
            var x = ReadNumber(textNode.Attribute("x"), 0);
            var y = ReadNumber(textNode.Attribute("y"), 0);

            if (isIndicator && rect is not null)
            {
                var scale = 1.5;
                var match = Regex.Match(transform, @"translate\(([-0-9.]+)\s+([-0-9.]+)\)");
                var cx = match.Success ? double.Parse(match.Groups[1].Value, CultureInfo.InvariantCulture) : x;
                var cy = match.Success ? double.Parse(match.Groups[2].Value, CultureInfo.InvariantCulture) : y;
                double TransformX(double value) => cx + (value - cx) * scale;
                double TransformY(double value) => cy + (value - cy) * scale;

                var rectX = TransformX(ReadNumber(rect.Attribute("x"), 0));
                var rectY = TransformY(ReadNumber(rect.Attribute("y"), 0));
                var width = ReadNumber(rect.Attribute("width"), 120) * scale;
                var height = ReadNumber(rect.Attribute("height"), 42) * scale;
                var fillColor = (string?)rect.Attribute("fill") ?? "#102A4A";
                var strokeColor = (string?)rect.Attribute("stroke") ?? "#1688FF";
                var strokeWidth = ReadNumber(rect.Attribute("stroke-width"), 1) * scale;

                var element = new EnergyMapElement
                {
                    Id = "indicator-" + (++index).ToString(CultureInfo.InvariantCulture),
                    Kind = "Indicator",
                    Text = text,
                    X = rectX,
                    Y = rectY,
                    Width = width,
                    Height = height,
                    FontSize = fontSize * scale,
                    FontFamily = fontFamily,
                    Foreground = NormalizeColor(fill),
                    Background = NormalizeColor(fillColor),
                    BorderColor = NormalizeColor(strokeColor),
                    BorderWidth = strokeWidth,
                    CornerRadius = ReadNumber(rect.Attribute("rx"), 0) * scale,
                    Bold = weight == "700" || weight.Equals("bold", StringComparison.OrdinalIgnoreCase)
                };
                elements.Add(element);
                rect.Remove();
                textNode.Remove();
            }
            else
            {
                var width = Math.Max(80, text.Length * fontSize * 0.72);
                elements.Add(new EnergyMapElement
                {
                    Id = "text-" + (++index).ToString(CultureInfo.InvariantCulture),
                    Kind = "Text",
                    Text = text,
                    X = x - width / 2,
                    Y = y - fontSize * 1.15,
                    Width = width,
                    Height = fontSize * 1.55,
                    FontSize = fontSize,
                    FontFamily = fontFamily,
                    Foreground = NormalizeColor(fill),
                    AccentText = accentText,
                    AccentForeground = NormalizeColor(accentFill),
                    Bold = weight == "700" || weight.Equals("bold", StringComparison.OrdinalIgnoreCase)
                });
                textNode.Remove();
            }
        }

        backgroundSvg = document.ToString(SaveOptions.DisableFormatting);
        return elements;
    }

    private static double ReadNumber(XAttribute? attribute, double fallback)
    {
        return attribute is not null &&
               double.TryParse(attribute.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var value)
            ? value : fallback;
    }

    private static string NormalizeColor(string value)
    {
        if (value.StartsWith('#') && value.Length == 4)
            return "#FF" + string.Concat(value.Skip(1).Select(c => new string(c, 2)));
        if (value.StartsWith('#') && value.Length == 7)
            return "#FF" + value[1..];
        return value;
    }
}

internal static class EnergyMapEditorSystemTheme
{

    public static MessageBoxResult ShowMessage(Window? owner, string message, string caption,
        MessageBoxButton buttons = MessageBoxButton.OK, MessageBoxImage image = MessageBoxImage.None)
    {
        var dialog = new Window
        {
            Title = caption,
            Width = 480,
            MinWidth = 380,
            MaxWidth = 720,
            SizeToContent = SizeToContent.Height,
            MinHeight = 170,
            ResizeMode = ResizeMode.NoResize,
            WindowStartupLocation = owner is null ? WindowStartupLocation.CenterScreen : WindowStartupLocation.CenterOwner,
            Owner = owner,
            ShowInTaskbar = false
        };

        var root = new DockPanel { Margin = new Thickness(20) };
        var buttonsPanel = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 18, 0, 0)
        };
        DockPanel.SetDock(buttonsPanel, Dock.Bottom);

        var messageRow = new StackPanel { Orientation = Orientation.Horizontal };
        var glyph = image switch
        {
            MessageBoxImage.Warning => "⚠",
            MessageBoxImage.Error => "✖",
            MessageBoxImage.Question => "?",
            MessageBoxImage.Information => "ℹ",
            _ => ""
        };
        if (glyph.Length > 0)
            messageRow.Children.Add(new TextBlock
            {
                Text = glyph,
                FontSize = 22,
                Margin = new Thickness(0, 0, 14, 0),
                VerticalAlignment = VerticalAlignment.Top,
                Foreground = Brushes.White
            });
        messageRow.Children.Add(new TextBlock
        {
            Text = message,
            TextWrapping = TextWrapping.Wrap,
            MaxWidth = 620,
            VerticalAlignment = VerticalAlignment.Center,
            Foreground = Brushes.White
        });

        var choices = buttons switch
        {
            MessageBoxButton.OKCancel => new[]
            {
                (MessageBoxResult.OK, "ОК", true, false),
                (MessageBoxResult.Cancel, "Отмена", false, true)
            },
            MessageBoxButton.YesNo => new[]
            {
                (MessageBoxResult.Yes, "Да", true, false),
                (MessageBoxResult.No, "Нет", false, false)
            },
            MessageBoxButton.YesNoCancel => new[]
            {
                (MessageBoxResult.Yes, "Да", true, false),
                (MessageBoxResult.No, "Нет", false, false),
                (MessageBoxResult.Cancel, "Отмена", false, true)
            },
            MessageBoxButton.RetryCancel => new[]
            {
                (MessageBoxResult.Retry, "Повторить", true, false),
                (MessageBoxResult.Cancel, "Отмена", false, true)
            },
            _ => new[]
            {
                (MessageBoxResult.OK, "ОК", true, false)
            }
        };

        var result = MessageBoxResult.None;
        foreach (var choice in choices)
        {
            var button = new Button
            {
                Content = choice.Item2,
                MinWidth = 88,
                Padding = new Thickness(14, 7, 14, 7),
                Margin = new Thickness(8, 0, 0, 0),
                Tag = choice.Item3 ? "SystemPrimaryButton" : null,
                IsDefault = choice.Item3,
                IsCancel = choice.Item4
            };
            button.Click += (_, _) =>
            {
                result = choice.Item1;
                dialog.DialogResult = true;
            };
            buttonsPanel.Children.Add(button);
        }

        root.Children.Add(buttonsPanel);
        root.Children.Add(messageRow);
        dialog.Content = root;
        dialog.Loaded += (_, _) => Apply(dialog);
        dialog.Closed += (_, _) =>
        {
            if (result == MessageBoxResult.None)
                result = buttons == MessageBoxButton.YesNo ? MessageBoxResult.No : MessageBoxResult.Cancel;
        };
        dialog.ShowDialog();
        return result == MessageBoxResult.None
            ? buttons == MessageBoxButton.YesNo ? MessageBoxResult.No : MessageBoxResult.Cancel
            : result;
    }

    private static readonly Brush DefaultSurfaceBrush = new SolidColorBrush(Color.FromRgb(58, 58, 58));

    public static void Apply(Window window, params DependencyObject[] excluded)
    {
        Brush Resource(string key) => window.TryFindResource(key) as Brush ?? DefaultSurfaceBrush;
        var textBrush = Resource("SystemPrimaryTextBrush");
        var inputTextBrush = Resource("SystemInputTextBrush");
        var buttonTextBrush = Resource("SystemButtonTextBrush");
        var primaryButtonTextBrush = Resource("SystemPrimaryButtonTextBrush");
        var surfaceBrush = Resource("SystemSurfaceBackgroundBrush");
        var dialogBrush = Resource("SystemDialogBackgroundBrush");
        var inputBrush = Resource("SystemInputBackgroundBrush");
        var buttonBrush = Resource("SystemButtonBackgroundBrush");
        var primaryButtonBrush = Resource("SystemPrimaryButtonBackgroundBrush");

        window.Background = dialogBrush;
        window.Foreground = textBrush;
        var skip = new HashSet<DependencyObject>(excluded);
        void Visit(DependencyObject parent)
        {
            if (skip.Contains(parent)) return;
            if (parent is Button button)
            {
                var isPrimary = string.Equals(button.Tag?.ToString(), "SystemPrimaryButton", StringComparison.Ordinal) ||
                    button.Background is SolidColorBrush originalButtonBrush &&
                    originalButtonBrush.Color == Color.FromRgb(245, 185, 66);
                button.Foreground = isPrimary ? primaryButtonTextBrush : buttonTextBrush;
                button.Background = isPrimary ? primaryButtonBrush : buttonBrush;
            }
            else if (parent is TextBox or PasswordBox or ComboBox or ComboBoxItem)
            {
                if (parent is Control input)
                {
                    input.Foreground = inputTextBrush;
                    input.Background = inputBrush;
                }
            }
            else if (parent is Control control)
            {
                control.Foreground = textBrush;
                control.Background = surfaceBrush;
            }
            if (parent is TextBlock textBlock)
            {
                if (textBlock.Foreground is null ||
                    textBlock.Foreground is SolidColorBrush solid && solid.Color == Colors.White)
                    textBlock.Foreground = textBrush;
            }
            if (parent is Border border) border.Background = surfaceBrush;
            if (parent is Panel panel && panel.Background is not null) panel.Background = surfaceBrush;
            for (var i = 0; i < VisualTreeHelper.GetChildrenCount(parent); i++) Visit(VisualTreeHelper.GetChild(parent, i));
        }
        var itemStyle = new Style(typeof(ComboBoxItem));
        itemStyle.Setters.Add(new Setter(Control.BackgroundProperty, inputBrush));
        itemStyle.Setters.Add(new Setter(Control.ForegroundProperty, inputTextBrush));
        var hover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true };
        hover.Setters.Add(new Setter(Control.BackgroundProperty, surfaceBrush));
        hover.Setters.Add(new Setter(Control.ForegroundProperty, inputTextBrush));
        itemStyle.Triggers.Add(hover);
        window.Resources[typeof(ComboBoxItem)] = itemStyle;
        Visit(window);
    }
}

internal sealed class EnergyMapEditorWindow : Window
{
    private readonly List<EnergyMapElement> _elements;
    private readonly ListBox _list = new() { Width = 245, Margin = new Thickness(0, 0, 14, 0) };
    private readonly Dictionary<string, TextBox> _fields = new();
    private readonly ComboBox _dataSource = new() { MinWidth = 220, Height = 28 };
    private EnergyMapElement? _editingItem;
    private readonly CheckBox _bold = new() { Content = "Жирный шрифт", Foreground = Brushes.White, Margin = new Thickness(0, 8, 0, 0) };

    public List<EnergyMapElement> Elements => _elements;

    public EnergyMapEditorWindow(IEnumerable<EnergyMapElement> elements, Window owner)
    {
        _elements = elements.Select(item => item.Copy()).ToList();
        Owner = owner;
        Title = "Редактор энергосхемы";
        Width = 900;
        Height = 650;
        MinWidth = 760;
        MinHeight = 540;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = new SolidColorBrush(Color.FromRgb(58, 58, 58));
        Foreground = Brushes.White;

        var root = new DockPanel { Margin = new Thickness(16) };
        var footer = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 14, 0, 0)
        };
        DockPanel.SetDock(footer, Dock.Bottom);

        var addText = MakeButton("Добавить текст");
        addText.Click += (_, _) => AddElement("Text");
        var addIndicator = MakeButton("Добавить индикатор");
        addIndicator.Click += (_, _) => AddElement("Indicator");
        var delete = MakeButton("Удалить");
        delete.Click += (_, _) => DeleteSelected();
        var save = MakeButton("Сохранить");
        save.Background = new SolidColorBrush(Color.FromRgb(245, 185, 66));
        save.Foreground = new SolidColorBrush(Color.FromRgb(255, 255, 255));
        save.FontWeight = FontWeights.Bold;
        save.Click += (_, _) =>
        {
            ApplyFields();
            DialogResult = true;
        };
        var cancel = MakeButton("Отмена");
        cancel.Click += (_, _) => DialogResult = false;

        footer.Children.Add(cancel);
        footer.Children.Add(save);
        DockPanel.SetDock(footer, Dock.Bottom);
        root.Children.Add(footer);

        var actions = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(0, 0, 0, 12) };
        actions.Children.Add(addText);
        actions.Children.Add(addIndicator);
        actions.Children.Add(delete);
        DockPanel.SetDock(actions, Dock.Top);
        root.Children.Add(actions);

        var body = new Grid();
        body.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(260) });
        body.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        _list.DisplayMemberPath = "Text";
        _list.Background = new SolidColorBrush(Color.FromRgb(58, 58, 58));
        _list.Foreground = Brushes.White;
        _list.BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124));
        var listItemStyle = new Style(typeof(ListBoxItem));
        listItemStyle.Setters.Add(new Setter(Control.BackgroundProperty, Brushes.Transparent));
        listItemStyle.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        listItemStyle.Setters.Add(new Setter(Control.PaddingProperty, new Thickness(7, 5, 7, 5)));
        var listHover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true };
        listHover.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(74, 74, 74))));
        listItemStyle.Triggers.Add(listHover);
        var listSelected = new Trigger { Property = ListBoxItem.IsSelectedProperty, Value = true };
        listSelected.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(74, 74, 74))));
        listSelected.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        listItemStyle.Triggers.Add(listSelected);
        _list.ItemContainerStyle = listItemStyle;
        _list.SelectionChanged += (_, _) =>
        {
            ApplyFields();
            LoadFields();
        };
        Grid.SetColumn(_list, 0);
        body.Children.Add(_list);

        var editorScroll = new ScrollViewer { VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        var editor = new StackPanel { Margin = new Thickness(8, 0, 0, 0) };
        AddField(editor, "Текст / значение", "Text");
        AddField(editor, "X", "X");
        AddField(editor, "Y", "Y");
        AddField(editor, "Ширина", "Width");
        AddField(editor, "Высота", "Height");
        AddField(editor, "Размер шрифта", "FontSize");
        AddField(editor, "Шрифт", "FontFamily");
        editor.Children.Add(_bold);
        AddField(editor, "Цвет текста", "Foreground");
        AddField(editor, "Акцентный фрагмент", "AccentText");
        AddField(editor, "Цвет акцента", "AccentForeground");
        AddField(editor, "Цвет фона", "Background");
        AddField(editor, "Цвет рамки", "BorderColor");
        AddField(editor, "Толщина рамки", "BorderWidth");
        AddField(editor, "Скругление", "CornerRadius");
        editor.Children.Add(new TextBlock { Text = "Источник данных", Margin = new Thickness(0, 8, 0, 4), Foreground = new SolidColorBrush(Color.FromRgb(255, 255, 255)) });
        foreach (var item in new[] { "", "Pv1Power", "Pv1Voltage", "Pv1Current", "Pv2Power", "Pv2Voltage", "Pv2Current", "InverterPower" })
            _dataSource.Items.Add(item);
        _dataSource.Background = new SolidColorBrush(Color.FromRgb(58, 58, 58));
        _dataSource.Foreground = new SolidColorBrush(Color.FromRgb(255, 255, 255));
        _dataSource.BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124));
        ApplyDarkComboBoxItems(_dataSource);
        editor.Children.Add(_dataSource);
        editorScroll.Content = editor;
        Grid.SetColumn(editorScroll, 1);
        body.Children.Add(editorScroll);
        root.Children.Add(body);

        Content = root;
        Loaded += (_, _) => EnergyMapEditorSystemTheme.Apply(this);
        RefreshList();
        if (_elements.Count > 0) _list.SelectedIndex = 0;
    }

    private static void ApplyDarkComboBoxItems(ComboBox combo)
    {
        var itemStyle = new Style(typeof(ComboBoxItem));
        itemStyle.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        itemStyle.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(58, 58, 58))));
        itemStyle.Setters.Add(new Setter(Control.PaddingProperty, new Thickness(7, 5, 7, 5)));
        var hover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true };
        hover.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        hover.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(74, 74, 74))));
        itemStyle.Triggers.Add(hover);
        var selected = new Trigger { Property = ComboBoxItem.IsSelectedProperty, Value = true };
        selected.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        selected.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(58, 58, 58))));
        itemStyle.Triggers.Add(selected);
        combo.ItemContainerStyle = itemStyle;

        // App.xaml задаёт глобальный Foreground для TextBlock. Локальный стиль
        // позволяет тексту наследовать цвет от ComboBox/ComboBoxItem:
        // выбранное значение остаётся светлым, пункты списка — светлыми.
        combo.Resources[typeof(TextBlock)] = new Style(typeof(TextBlock));

        combo.Resources[SystemColors.WindowBrushKey] = new SolidColorBrush(Color.FromRgb(58, 58, 58));
        combo.Resources[SystemColors.WindowTextBrushKey] = Brushes.White;
        combo.Resources[SystemColors.ControlBrushKey] = new SolidColorBrush(Color.FromRgb(58, 58, 58));
        combo.Resources[SystemColors.ControlTextBrushKey] = Brushes.White;
        combo.Resources[SystemColors.HighlightBrushKey] = new SolidColorBrush(Color.FromRgb(58, 58, 58));
        combo.Resources[SystemColors.HighlightTextBrushKey] = Brushes.White;

    }

    private static Button MakeButton(string text) => new()
    {
        Content = text,
        Padding = new Thickness(12, 7, 12, 7),
        Margin = new Thickness(0, 0, 8, 0),
        Background = new SolidColorBrush(Color.FromRgb(58, 58, 58)),
        Foreground = Brushes.White,
        BorderThickness = new Thickness(0)
    };

    private void AddField(Panel parent, string label, string key)
    {
        parent.Children.Add(new TextBlock { Text = label, Margin = new Thickness(0, 8, 0, 4), Foreground = Brushes.White });
        var box = new TextBox
        {
            Height = 30,
            Padding = new Thickness(7, 4, 7, 4),
            Background = new SolidColorBrush(Color.FromRgb(36, 54, 74)),
            Foreground = Brushes.White,
            BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124)),
            CaretBrush = Brushes.White,
            SelectionBrush = new SolidColorBrush(Color.FromRgb(65, 112, 158))
        };
        _fields[key] = box;
        parent.Children.Add(box);
    }

    private void AddElement(string kind)
    {
        ApplyFields();
        var element = new EnergyMapElement
        {
            Id = Guid.NewGuid().ToString("N"),
            Kind = kind,
            Text = kind == "Indicator" ? "Новое значение" : "Новый текст",
            X = 600,
            Y = 400,
            Width = kind == "Indicator" ? 190 : 220,
            Height = kind == "Indicator" ? 58 : 36,
            FontSize = 22,
            Foreground = "#FFFFFFFF",
            Background = kind == "Indicator" ? "#E6103155" : "#00000000",
            BorderColor = kind == "Indicator" ? "#FF8BC9F5" : "#00000000",
            BorderWidth = kind == "Indicator" ? 1 : 0,
            CornerRadius = 6,
            Bold = kind == "Indicator"
        };
        _elements.Add(element);
        RefreshList();
        _list.SelectedItem = element;
    }

    private void DeleteSelected()
    {
        ApplyFields();
        if (_list.SelectedItem is not EnergyMapElement selected) return;
        _elements.Remove(selected);
        RefreshList();
        if (_elements.Count > 0) _list.SelectedIndex = 0;
    }

    private void RefreshList()
    {
        _list.ItemsSource = null;
        _list.ItemsSource = _elements;
    }

    private void LoadFields()
    {
        if (_list.SelectedItem is not EnergyMapElement item)
        {
            _editingItem = null;
            return;
        }
        _editingItem = item;
        _fields["Text"].Text = item.Text;
        _fields["X"].Text = F(item.X);
        _fields["Y"].Text = F(item.Y);
        _fields["Width"].Text = F(item.Width);
        _fields["Height"].Text = F(item.Height);
        _fields["FontSize"].Text = F(item.FontSize);
        _fields["FontFamily"].Text = item.FontFamily;
        _bold.IsChecked = item.Bold;
        _fields["Foreground"].Text = item.Foreground;
        _fields["AccentText"].Text = item.AccentText;
        _fields["AccentForeground"].Text = item.AccentForeground;
        _fields["Background"].Text = item.Background;
        _fields["BorderColor"].Text = item.BorderColor;
        _fields["BorderWidth"].Text = F(item.BorderWidth);
        _fields["CornerRadius"].Text = F(item.CornerRadius);
        _dataSource.SelectedItem = item.DataSource;
    }

    private void ApplyFields()
    {
        if (_editingItem is not EnergyMapElement item) return;
        item.Text = _fields["Text"].Text;
        item.X = N(_fields["X"].Text, item.X);
        item.Y = N(_fields["Y"].Text, item.Y);
        var newWidth = Math.Max(12, N(_fields["Width"].Text, item.Width));
        var newHeight = Math.Max(12, N(_fields["Height"].Text, item.Height));
        item.TextScaleX *= newWidth / item.Width;
        item.TextScaleY *= newHeight / item.Height;
        item.Width = newWidth;
        item.Height = newHeight;
        item.FontSize = Math.Clamp(N(_fields["FontSize"].Text, item.FontSize), 6, 160);
        item.FontFamily = _fields["FontFamily"].Text;
        item.Bold = _bold.IsChecked == true;
        item.Foreground = _fields["Foreground"].Text;
        item.AccentText = _fields["AccentText"].Text;
        item.AccentForeground = _fields["AccentForeground"].Text;
        item.Background = _fields["Background"].Text;
        item.BorderColor = _fields["BorderColor"].Text;
        item.BorderWidth = Math.Max(0, N(_fields["BorderWidth"].Text, item.BorderWidth));
        item.CornerRadius = Math.Max(0, N(_fields["CornerRadius"].Text, item.CornerRadius));
        item.DataSource = _dataSource.SelectedItem as string ?? "";
        _list.Items.Refresh();
    }

    private static string F(double value) => value.ToString("0.##", CultureInfo.InvariantCulture);
    private static double N(string value, double fallback) =>
        double.TryParse(value.Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out var number)
            ? number : fallback;
}

internal sealed class EnergyMapPropertiesDialog : Window
{
    private readonly TextBox _widthBox;
    private readonly TextBox _heightBox;
    private readonly TextBox? _textBox;
    private readonly TextBox _fontSizeBox;
    private readonly TextBox _accentTextBox;
    private readonly ComboBox _dataSourceBox;
    private readonly ComboBox _backgroundBox;
    private readonly ComboBox _borderBox;
    private readonly ComboBox _textColorBox;
    private readonly ComboBox _accentColorBox;
    private readonly ComboBox _shapeBox;
    private readonly ComboBox _fontBox;
    private readonly CheckBox _boldBox;
    private readonly Border _preview;
    private readonly TextBlock _previewText;

    public double ElementWidth { get; private set; }
    public double ElementHeight { get; private set; }
    public double ElementTextScaleX { get; private set; }
    public double ElementTextScaleY { get; private set; }
    public double ElementFontSize { get; private set; }
    public double ElementBorderWidth { get; private set; }
    public double ElementCornerRadius { get; private set; }
    public string ElementText { get; private set; } = "";
    public string ElementBackground { get; private set; } = "#00000000";
    public string ElementBorderColor { get; private set; } = "#00000000";
    public string ElementForeground { get; private set; } = "#FFFFFFFF";
    public string ElementAccentText { get; private set; } = "";
    public string ElementAccentForeground { get; private set; } = "#FFFFFFFF";
    public string ElementFontFamily { get; private set; } = "Arial";
    public bool ElementBold { get; private set; }
    public string DataSource { get; private set; } = "";
    public bool DeleteRequested { get; private set; }

    private static readonly (string Name, string Value)[] Palette =
    {
        ("Прозрачный", "#00000000"),
        ("Белый", "#FFFFFFFF"),
        ("Светло-серый", "#FFDCE6F0"),
        ("Серый", "#FF9EADBC"),
        ("Тёмный", "#E6101E2E"),
        ("Синий", "#E61B4770"),
        ("Голубой", "#FF55B8F2"),
        ("Зелёный", "#FF55D69E"),
        ("Жёлтый", "#FFFFC857"),
        ("Оранжевый", "#FFFF984F"),
        ("Красный", "#FFFF6868"),
        ("Фиолетовый", "#FFB69CFF")
    };

    public EnergyMapPropertiesDialog(EnergyMapElement element, Window owner)
    {
        Owner = owner;
        Title = "Свойства информационного блока";
        Width = 510;
        Height = 790;
        MinWidth = 440;
        MinHeight = 650;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ResizeMode = ResizeMode.CanResize;
        Background = new SolidColorBrush(Color.FromRgb(18, 30, 48));
        Foreground = Brushes.White;

        var root = new DockPanel { Margin = new Thickness(18) };
        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 12, 0, 0)
        };
        DockPanel.SetDock(buttons, Dock.Bottom);

        var cancel = MakeButton("Отмена", false);
        cancel.IsCancel = true;
        var delete = MakeButton("Удалить", false);
        delete.Background = new SolidColorBrush(Color.FromRgb(150, 48, 55));
        delete.Click += (_, _) =>
        {
            var result = EnergyMapEditorSystemTheme.ShowMessage(this,
                "Удалить выбранный информационный блок со схемы? Это действие нельзя отменить.",
                "Удаление объекта", MessageBoxButton.YesNo, MessageBoxImage.Warning);
            if (result != MessageBoxResult.Yes) return;
            DeleteRequested = true;
            DialogResult = true;
        };
        var save = MakeButton("Сохранить", true);
        save.IsDefault = true;
        buttons.Children.Add(delete);
        buttons.Children.Add(cancel);
        buttons.Children.Add(save);
        root.Children.Add(buttons);

        var fields = new StackPanel();
        var sizeRow = new Grid();
        sizeRow.ColumnDefinitions.Add(new ColumnDefinition());
        sizeRow.ColumnDefinitions.Add(new ColumnDefinition());
        AddField(sizeRow, "Ширина", out _widthBox, element.Width, 0);
        AddField(sizeRow, "Высота", out _heightBox, element.Height, 1);
        fields.Children.Add(sizeRow);

        if (element.Kind == "Text")
        {
            AddLabel(fields, "Текст");
            _textBox = new TextBox
            {
                Text = element.Text, Height = 58, AcceptsReturn = true,
                TextWrapping = TextWrapping.Wrap, Padding = new Thickness(7),
                Background = InputBackground(), Foreground = Brushes.White,
                BorderBrush = InputBorder(), CaretBrush = Brushes.White
            };
            fields.Children.Add(_textBox);
        }

        AddLabel(fields, "Размер текста");
        _fontSizeBox = new TextBox
        {
            Text = element.FontSize.ToString("0.##", CultureInfo.InvariantCulture),
            Height = 30, Padding = new Thickness(7, 4, 7, 4),
            Background = InputBackground(), Foreground = Brushes.White,
            BorderBrush = InputBorder(), CaretBrush = Brushes.White
        };
        fields.Children.Add(_fontSizeBox);

        AddLabel(fields, "Шрифт");
        _fontBox = MakeCombo(fields, new[] { "Arial", "Segoe UI", "Consolas", "Verdana" });
        _fontBox.IsEditable = true;
        _fontBox.IsReadOnly = true;
        _fontBox.Resources[typeof(TextBox)] = new Style(typeof(TextBox))
        {
            Setters =
            {
                new Setter(Control.BackgroundProperty, InputBackground()),
                new Setter(Control.ForegroundProperty, Brushes.White),
                new Setter(Control.BorderThicknessProperty, new Thickness(0)),
                new Setter(Control.PaddingProperty, new Thickness(7, 4, 7, 4)),
                new Setter(Control.VerticalContentAlignmentProperty, VerticalAlignment.Center)
            }
        };
        _fontBox.SelectedItem = NormalizeFont(element.FontFamily);

        _boldBox = new CheckBox
        {
            Content = "Жирный текст",
            IsChecked = element.Bold,
            Foreground = Brushes.White,
            Margin = new Thickness(0, 9, 0, 2)
        };
        fields.Children.Add(_boldBox);

        AddLabel(fields, "Цвет текста");
        _textColorBox = MakePaletteCombo(fields, element.Foreground);
        AddLabel(fields, "Фон блока");
        _backgroundBox = MakePaletteCombo(fields, element.Background);
        AddLabel(fields, "Цвет рамки");
        _borderBox = MakePaletteCombo(fields, element.BorderColor);

        AddLabel(fields, "Форма блока");
        _shapeBox = MakeCombo(fields, new[]
        {
            "Прямоугольник", "Слегка скруглённый", "Скруглённый", "Капсула"
        });
        _shapeBox.SelectedIndex = element.CornerRadius <= 1 ? 0 :
            element.CornerRadius <= 7 ? 1 : element.CornerRadius <= 15 ? 2 : 3;

        AddLabel(fields, "Толщина рамки");
        var borderWidthBox = new TextBox
        {
            Text = element.BorderWidth.ToString("0.##", CultureInfo.InvariantCulture),
            Height = 30, Padding = new Thickness(7, 4, 7, 4),
            Background = InputBackground(), Foreground = Brushes.White,
            BorderBrush = InputBorder(), CaretBrush = Brushes.White
        };
        fields.Children.Add(borderWidthBox);

        save.Click += (_, _) => Save(element);

        AddLabel(fields, "Акцентный фрагмент текста");
        _accentTextBox = new TextBox
        {
            Text = element.AccentText, Height = 30, Padding = new Thickness(7, 4, 7, 4),
            Background = InputBackground(), Foreground = Brushes.White,
            BorderBrush = InputBorder(), CaretBrush = Brushes.White
        };
        fields.Children.Add(_accentTextBox);
        AddLabel(fields, "Цвет акцента");
        _accentColorBox = MakePaletteCombo(fields, element.AccentForeground);

        AddLabel(fields, "Источник данных");
        _dataSourceBox = MakeCombo(fields, new[]
        {
            "", "Pv1Power", "Pv1Voltage", "Pv1Current",
            "Pv2Power", "Pv2Voltage", "Pv2Current", "InverterPower"
        });
        _dataSourceBox.SelectedItem = element.DataSource;

        AddLabel(fields, "Предпросмотр");
        _previewText = new TextBlock
        {
            Text = element.Text, Foreground = Brushes.White, FontSize = element.FontSize,
            FontFamily = new FontFamily("Arial"), TextAlignment = TextAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center, TextWrapping = TextWrapping.Wrap
        };
        _preview = new Border
        {
            Height = 58, MinWidth = 120, Padding = new Thickness(6),
            Child = _previewText, Background = InputBackground(),
            BorderBrush = InputBorder(), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(6), Margin = new Thickness(0, 2, 0, 8)
        };
        fields.Children.Add(_preview);

        foreach (var control in new Control[]
        {
            _fontSizeBox, _fontBox, _boldBox, _textColorBox, _backgroundBox,
            _borderBox, _shapeBox, borderWidthBox, _accentTextBox, _accentColorBox
        })
        {
            if (control is TextBox tb) tb.TextChanged += (_, _) => UpdatePreview();
            else if (control is ComboBox cb) cb.SelectionChanged += (_, _) => UpdatePreview();
            else if (control is CheckBox check) check.Checked += (_, _) => UpdatePreview();
        }
        _boldBox.Unchecked += (_, _) => UpdatePreview();
        if (_textBox is not null) _textBox.TextChanged += (_, _) => UpdatePreview();
        UpdatePreview();

        fields.Children.Add(new TextBlock
        {
            Text = "Цвета выбираются из готовой палитры. Настройки сохраняются вместе с блоком.",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 7, 0, 0),
            Foreground = new SolidColorBrush(Color.FromRgb(255, 255, 255))
        });

        root.Children.Add(new ScrollViewer
        {
            Content = fields,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
        });
        Content = root;
        Loaded += (_, _) => EnergyMapEditorSystemTheme.Apply(this, _preview);

        void Save(EnergyMapElement source)
        {
            if (!TryNumber(_widthBox.Text, out var width) || width < 12 ||
                !TryNumber(_heightBox.Text, out var height) || height < 12 ||
                !TryNumber(_fontSizeBox.Text, out var fontSize) || fontSize < 6 || fontSize > 160 ||
                !TryNumber(borderWidthBox.Text, out var borderWidth) || borderWidth < 0 || borderWidth > 20)
            {
                EnergyMapEditorSystemTheme.ShowMessage(this, "Проверьте размеры, размер шрифта и толщину рамки. Значения должны быть в допустимом диапазоне.",
                    "Свойства блока", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            ElementWidth = width;
            ElementHeight = height;
            ElementTextScaleX = source.TextScaleX * (width / source.Width);
            ElementTextScaleY = source.TextScaleY * (height / source.Height);
            ElementFontSize = fontSize;
            ElementBorderWidth = borderWidth;
            ElementCornerRadius = _shapeBox.SelectedIndex switch
            {
                0 => 0,
                1 => 5,
                2 => 12,
                _ => Math.Min(width, height) / 2
            };
            ElementText = _textBox?.Text ?? source.Text;
            ElementBackground = SelectedColor(_backgroundBox, source.Background);
            ElementBorderColor = SelectedColor(_borderBox, source.BorderColor);
            ElementForeground = SelectedColor(_textColorBox, source.Foreground);
            ElementAccentText = _accentTextBox.Text;
            ElementAccentForeground = SelectedColor(_accentColorBox, source.AccentForeground);
            ElementFontFamily = _fontBox.SelectedItem as string ?? "Arial";
            ElementBold = _boldBox.IsChecked == true;
            DataSource = _dataSourceBox.SelectedItem as string ?? "";
            DialogResult = true;
        }
    }

    private void UpdatePreview()
    {
        var previewValue = _textBox?.Text ?? "Пример показания";
        _previewText.Inlines.Clear();
        _previewText.Foreground = PaletteBrush(_textColorBox);
        var accent = _accentTextBox.Text;
        var accentIndex = string.IsNullOrEmpty(accent)
            ? -1 : previewValue.IndexOf(accent, StringComparison.Ordinal);
        if (accentIndex >= 0)
        {
            if (accentIndex > 0)
                _previewText.Inlines.Add(new System.Windows.Documents.Run(previewValue[..accentIndex]));
            _previewText.Inlines.Add(new System.Windows.Documents.Run(accent)
            {
                Foreground = PaletteBrush(_accentColorBox),
                FontWeight = FontWeights.Bold
            });
            var tail = previewValue[(accentIndex + accent.Length)..];
            if (tail.Length > 0) _previewText.Inlines.Add(new System.Windows.Documents.Run(tail));
        }
        else _previewText.Text = previewValue;
        _previewText.FontSize = TryNumber(_fontSizeBox.Text, out var size) ? Math.Clamp(size, 6, 48) : 18;
        _previewText.FontFamily = new FontFamily(_fontBox.SelectedItem as string ?? "Arial");
        _previewText.FontWeight = _boldBox.IsChecked == true ? FontWeights.Bold : FontWeights.Normal;
        _preview.Background = PaletteBrush(_backgroundBox);
        _preview.BorderBrush = PaletteBrush(_borderBox);
        _preview.BorderThickness = new Thickness(1);
        _preview.CornerRadius = _shapeBox.SelectedIndex switch
        {
            0 => new CornerRadius(0),
            1 => new CornerRadius(5),
            2 => new CornerRadius(12),
            _ => new CornerRadius(28)
        };
    }

    private static Button MakeButton(string text, bool primary) => new()
    {
        Content = text, Padding = new Thickness(14, 7, 14, 7),
        Margin = new Thickness(0, 0, 8, 0),
        Background = primary
            ? new SolidColorBrush(Color.FromRgb(245, 185, 66))
            : new SolidColorBrush(Color.FromRgb(38, 56, 77)),
        Foreground = primary
            ? Brushes.White
            : Brushes.White,
        BorderThickness = new Thickness(0),
        FontWeight = primary ? FontWeights.Bold : FontWeights.Normal
    };

    private static SolidColorBrush InputBackground() => new(Color.FromRgb(36, 54, 74));
    private static SolidColorBrush InputBorder() => new(Color.FromRgb(73, 98, 124));

    private static void AddLabel(Panel parent, string label) =>
        parent.Children.Add(new TextBlock
        {
            Text = label, Margin = new Thickness(0, 8, 0, 4),
            Foreground = Brushes.White
        });

    private static void AddField(Grid parent, string label, out TextBox box, double value, int column)
    {
        var panel = new StackPanel { Margin = new Thickness(column == 0 ? 0 : 8, 0, 0, 0) };
        AddLabel(panel, label);
        box = new TextBox
        {
            Text = value.ToString("0.##", CultureInfo.InvariantCulture),
            Height = 30, Padding = new Thickness(7, 4, 7, 4),
            Background = InputBackground(), Foreground = Brushes.White,
            BorderBrush = InputBorder(), CaretBrush = Brushes.White
        };
        panel.Children.Add(box);
        Grid.SetColumn(panel, column);
        parent.Children.Add(panel);
    }

    private static ComboBox MakeCombo(Panel parent, IEnumerable<string> values)
    {
        var combo = new ComboBox
        {
            Height = 30, Background = InputBackground(), Foreground = Brushes.White,
            BorderBrush = InputBorder()
        };
        ApplyDarkComboBoxItems(combo);
        foreach (var value in values) combo.Items.Add(value);
        parent.Children.Add(combo);
        return combo;
    }

    private static ComboBox MakePaletteCombo(Panel parent, string selected)
    {
        var combo = new ComboBox
        {
            Height = 30, Background = InputBackground(), Foreground = Brushes.White,
            BorderBrush = InputBorder(), DisplayMemberPath = "Name",
            SelectedValuePath = "Value"
        };
        ApplyDarkComboBoxItems(combo);
        foreach (var item in Palette)
            combo.Items.Add(new PaletteItem(item.Name, item.Value));
        if (!Palette.Any(item => item.Value.Equals(selected, StringComparison.OrdinalIgnoreCase)))
            combo.Items.Add(new PaletteItem("Текущий цвет", selected));
        combo.SelectedValue = selected;
        parent.Children.Add(combo);
        return combo;
    }

    private static string SelectedColor(ComboBox combo, string fallback) =>
        combo.SelectedValue as string ?? fallback;

    private static Brush PaletteBrush(ComboBox combo)
    {
        var value = combo.SelectedValue as string ?? "#FFFFFFFF";
        try { return new SolidColorBrush((Color)ColorConverter.ConvertFromString(value)); }
        catch { return Brushes.Transparent; }
    }

    private static string NormalizeFont(string value) =>
        new[] { "Arial", "Segoe UI", "Consolas", "Verdana" }
            .FirstOrDefault(font => font.Equals(value, StringComparison.OrdinalIgnoreCase)) ?? "Arial";

    private static bool TryNumber(string value, out double number) =>
        double.TryParse(value.Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out number);

    private sealed record PaletteItem(string Name, string Value)
    {
        public override string ToString() => Name;
    }

    private static void ApplyDarkComboBoxItems(ComboBox combo)
    {
        // Тёмный фон списка сочетается со светлым текстом, заданным глобальной темой.
        var itemStyle = new Style(typeof(ComboBoxItem));
        itemStyle.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        itemStyle.Setters.Add(new Setter(Control.BackgroundProperty, InputBackground()));
        itemStyle.Setters.Add(new Setter(Control.PaddingProperty, new Thickness(7, 5, 7, 5)));
        var hover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true };
        hover.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        hover.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(74, 74, 74))));
        itemStyle.Triggers.Add(hover);
        var selected = new Trigger { Property = ComboBoxItem.IsSelectedProperty, Value = true };
        selected.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        selected.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(58, 58, 58))));
        itemStyle.Triggers.Add(selected);
        combo.ItemContainerStyle = itemStyle;

        combo.Resources[SystemColors.WindowBrushKey] = InputBackground();
        combo.Resources[SystemColors.WindowTextBrushKey] = Brushes.White;
        combo.Resources[SystemColors.ControlBrushKey] = InputBackground();
        combo.Resources[SystemColors.ControlTextBrushKey] = Brushes.White;
        combo.Resources[SystemColors.HighlightBrushKey] = new SolidColorBrush(Color.FromRgb(58, 58, 58));
        combo.Resources[SystemColors.HighlightTextBrushKey] = Brushes.White;

        // Цвет текста выбранного значения в закрытом ComboBox.
        combo.Resources[typeof(TextBlock)] = new Style(typeof(TextBlock))
        {
            Setters =
            {
                new Setter(TextBlock.ForegroundProperty, Brushes.White)
            }
        };
    }
}

internal sealed class EnergyMapObjectPropertiesDialog : Window
{
    private readonly TextBox _bindingBox;

    public string BindingKey { get; private set; } = "";

    public EnergyMapObjectPropertiesDialog(string objectName, string bindingKey, Window owner)
    {
        Owner = owner;
        Title = "Свойства: " + objectName;
        Width = 440;
        Height = 250;
        MinWidth = 380;
        MinHeight = 220;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ResizeMode = ResizeMode.CanResize;
        Background = new SolidColorBrush(Color.FromRgb(18, 30, 48));
        Foreground = Brushes.White;

        var root = new DockPanel { Margin = new Thickness(18) };
        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 14, 0, 0)
        };
        DockPanel.SetDock(buttons, Dock.Bottom);

        var cancel = new Button
        {
            Content = "Отмена",
            Padding = new Thickness(14, 7, 14, 7),
            Margin = new Thickness(0, 0, 8, 0),
            Background = new SolidColorBrush(Color.FromRgb(38, 56, 77)),
            Foreground = Brushes.White,
            BorderThickness = new Thickness(0),
            IsCancel = true
        };
        var save = new Button
        {
            Content = "Сохранить",
            Padding = new Thickness(14, 7, 14, 7),
            Background = new SolidColorBrush(Color.FromRgb(245, 185, 66)),
            Foreground = Brushes.White,
            BorderThickness = new Thickness(0),
            FontWeight = FontWeights.Bold,
            IsDefault = true
        };
        save.Click += (_, _) =>
        {
            BindingKey = _bindingBox.Text.Trim();
            DialogResult = true;
        };
        buttons.Children.Add(cancel);
        buttons.Children.Add(save);
        root.Children.Add(buttons);

        var fields = new StackPanel();
        fields.Children.Add(new TextBlock
        {
            Text = "Ключ привязки к датчикам / функциям",
            Margin = new Thickness(0, 4, 0, 5),
            Foreground = Brushes.White
        });
        _bindingBox = new TextBox
        {
            Text = bindingKey,
            Height = 32,
            Padding = new Thickness(7, 5, 7, 5),
            Background = new SolidColorBrush(Color.FromRgb(36, 54, 74)),
            Foreground = Brushes.White,
            BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124)),
            CaretBrush = Brushes.White
        };
        fields.Children.Add(_bindingBox);
        fields.Children.Add(new TextBlock
        {
            Text = "Поле сохраняет идентификатор привязки. Подключение к датчикам и выполнение функций будут реализованы отдельно.",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 12, 0, 0),
            Foreground = Brushes.White
        });
        root.Children.Add(fields);
        Content = root;
        Loaded += (_, _) => EnergyMapEditorSystemTheme.Apply(this);
    }
}

internal sealed class EnergyMapTextDialog : Window
{
    private readonly TextBox _textBox;

    public string EditedText => _textBox.Text;

    public EnergyMapTextDialog(string text, Window owner)
    {
        Owner = owner;
        Title = "Редактирование текста";
        Width = 500;
        Height = 270;
        MinWidth = 380;
        MinHeight = 220;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        ResizeMode = ResizeMode.CanResize;
        Background = new SolidColorBrush(Color.FromRgb(18, 30, 48));
        Foreground = Brushes.White;

        var root = new DockPanel { Margin = new Thickness(16) };
        var buttons = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Right,
            Margin = new Thickness(0, 12, 0, 0)
        };
        DockPanel.SetDock(buttons, Dock.Bottom);

        var cancel = new Button
        {
            Content = "Отмена",
            Padding = new Thickness(14, 7, 14, 7),
            Margin = new Thickness(0, 0, 8, 0),
            Background = new SolidColorBrush(Color.FromRgb(38, 56, 77)),
            Foreground = Brushes.White,
            BorderThickness = new Thickness(0),
            IsCancel = true
        };
        var save = new Button
        {
            Content = "Сохранить",
            Padding = new Thickness(14, 7, 14, 7),
            Background = new SolidColorBrush(Color.FromRgb(245, 185, 66)),
            Foreground = Brushes.White,
            BorderThickness = new Thickness(0),
            FontWeight = FontWeights.Bold,
            IsDefault = true
        };
        save.Click += (_, _) => DialogResult = true;
        buttons.Children.Add(cancel);
        buttons.Children.Add(save);
        root.Children.Add(buttons);

        _textBox = new TextBox
        {
            Text = text,
            AcceptsReturn = true,
            TextWrapping = TextWrapping.Wrap,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            Padding = new Thickness(9),
            Background = new SolidColorBrush(Color.FromRgb(36, 54, 74)),
            Foreground = Brushes.White,
            BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124)),
            CaretBrush = Brushes.White,
            SelectionBrush = new SolidColorBrush(Color.FromRgb(65, 112, 158))
        };
        root.Children.Add(_textBox);
        Content = root;
        Loaded += (_, _) => EnergyMapEditorSystemTheme.Apply(this);
        Loaded += (_, _) =>
        {
            _textBox.Focus();
            _textBox.SelectAll();
        };
    }
}
