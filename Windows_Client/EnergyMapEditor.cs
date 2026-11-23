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
        Background = new SolidColorBrush(Color.FromRgb(18, 30, 48));
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
        save.Foreground = new SolidColorBrush(Color.FromRgb(23, 32, 45));
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
        _list.Background = new SolidColorBrush(Color.FromRgb(23, 38, 58));
        _list.Foreground = new SolidColorBrush(Color.FromRgb(240, 245, 250));
        _list.BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124));
        var listItemStyle = new Style(typeof(ListBoxItem));
        listItemStyle.Setters.Add(new Setter(Control.BackgroundProperty, Brushes.Transparent));
        listItemStyle.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        listItemStyle.Setters.Add(new Setter(Control.PaddingProperty, new Thickness(7, 5, 7, 5)));
        var listHover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true };
        listHover.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(41, 63, 88))));
        listItemStyle.Triggers.Add(listHover);
        var listSelected = new Trigger { Property = ListBoxItem.IsSelectedProperty, Value = true };
        listSelected.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(54, 81, 110))));
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
        editor.Children.Add(new TextBlock { Text = "Источник данных", Margin = new Thickness(0, 8, 0, 4), Foreground = new SolidColorBrush(Color.FromRgb(205, 216, 228)) });
        foreach (var item in new[] { "", "Pv1Power", "Pv1Voltage", "Pv1Current", "Pv2Power", "Pv2Voltage", "Pv2Current", "InverterPower" })
            _dataSource.Items.Add(item);
        _dataSource.Background = new SolidColorBrush(Color.FromRgb(36, 54, 74));
        _dataSource.Foreground = new SolidColorBrush(Color.FromRgb(244, 247, 251));
        _dataSource.BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124));
        ApplyDarkComboBoxItems(_dataSource);
        editor.Children.Add(_dataSource);
        editorScroll.Content = editor;
        Grid.SetColumn(editorScroll, 1);
        body.Children.Add(editorScroll);
        root.Children.Add(body);

        Content = root;
        RefreshList();
        if (_elements.Count > 0) _list.SelectedIndex = 0;
    }

    private static void ApplyDarkComboBoxItems(ComboBox combo)
    {
        var itemStyle = new Style(typeof(ComboBoxItem));
        itemStyle.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        itemStyle.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(36, 54, 74))));
        itemStyle.Setters.Add(new Setter(Control.PaddingProperty, new Thickness(7, 5, 7, 5)));
        var hover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true };
        hover.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        hover.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(65, 91, 120))));
        itemStyle.Triggers.Add(hover);
        var selected = new Trigger { Property = ComboBoxItem.IsSelectedProperty, Value = true };
        selected.Setters.Add(new Setter(Control.ForegroundProperty, Brushes.White));
        selected.Setters.Add(new Setter(Control.BackgroundProperty, new SolidColorBrush(Color.FromRgb(54, 81, 110))));
        itemStyle.Triggers.Add(selected);
        combo.ItemContainerStyle = itemStyle;
    }

    private static Button MakeButton(string text) => new()
    {
        Content = text,
        Padding = new Thickness(12, 7, 12, 7),
        Margin = new Thickness(0, 0, 8, 0),
        Background = new SolidColorBrush(Color.FromRgb(38, 56, 77)),
        Foreground = Brushes.White,
        BorderThickness = new Thickness(0)
    };

    private void AddField(Panel parent, string label, string key)
    {
        parent.Children.Add(new TextBlock { Text = label, Margin = new Thickness(0, 8, 0, 4), Foreground = new SolidColorBrush(Color.FromRgb(205, 216, 228)) });
        var box = new TextBox
        {
            Height = 30,
            Padding = new Thickness(7, 4, 7, 4),
            Background = new SolidColorBrush(Color.FromRgb(36, 54, 74)),
            Foreground = new SolidColorBrush(Color.FromRgb(244, 247, 251)),
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
        item.Width = Math.Max(12, N(_fields["Width"].Text, item.Width));
        item.Height = Math.Max(12, N(_fields["Height"].Text, item.Height));
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
    private readonly ComboBox _dataSourceBox;

    public double ElementWidth { get; private set; }
    public double ElementHeight { get; private set; }
    public string ElementText { get; private set; } = "";
    public string DataSource { get; private set; } = "";

    public EnergyMapPropertiesDialog(EnergyMapElement element, Window owner)
    {
        Owner = owner;
        Title = element.Kind == "Text" ? "Свойства текстового объекта" : "Свойства объекта";
        Width = 440;
        Height = element.Kind == "Text" ? 500 : 350;
        MinHeight = element.Kind == "Text" ? 470 : 330;
        MinWidth = 380;
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
            Foreground = new SolidColorBrush(Color.FromRgb(23, 32, 45)),
            BorderThickness = new Thickness(0),
            FontWeight = FontWeights.Bold,
            IsDefault = true
        };
        save.Click += (_, _) =>
        {
            if (!double.TryParse(_widthBox.Text.Replace(',', '.'), NumberStyles.Float,
                    CultureInfo.InvariantCulture, out var width) || width < 12 ||
                !double.TryParse(_heightBox.Text.Replace(',', '.'), NumberStyles.Float,
                    CultureInfo.InvariantCulture, out var height) || height < 12)
            {
                MessageBox.Show(this, "Ширина и высота должны быть числами не меньше 12.",
                    "Свойства объекта", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            ElementWidth = width;
            ElementHeight = height;
            ElementText = _textBox?.Text ?? element.Text;
            DataSource = _dataSourceBox.SelectedItem as string ?? "";
            DialogResult = true;
        };
        buttons.Children.Add(cancel);
        buttons.Children.Add(save);
        root.Children.Add(buttons);

        var fields = new StackPanel();
        AddField(fields, "Ширина", out _widthBox, element.Width);
        AddField(fields, "Высота", out _heightBox, element.Height);

        if (element.Kind == "Text")
        {
            fields.Children.Add(new TextBlock
            {
                Text = "Текст",
                Margin = new Thickness(0, 12, 0, 4),
                Foreground = new SolidColorBrush(Color.FromRgb(205, 216, 228))
            });
            _textBox = new TextBox
            {
                Text = element.Text,
                Height = 76,
                AcceptsReturn = true,
                TextWrapping = TextWrapping.Wrap,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Padding = new Thickness(7),
                Background = new SolidColorBrush(Color.FromRgb(36, 54, 74)),
                Foreground = new SolidColorBrush(Color.FromRgb(244, 247, 251)),
                BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124)),
                CaretBrush = Brushes.White
            };
            fields.Children.Add(_textBox);
        }

        fields.Children.Add(new TextBlock
        {
            Text = "Источник данных",
            Margin = new Thickness(0, 12, 0, 4),
            Foreground = new SolidColorBrush(Color.FromRgb(205, 216, 228))
        });
        _dataSourceBox = new ComboBox
        {
            Height = 30,
            Background = new SolidColorBrush(Color.FromRgb(36, 54, 74)),
            Foreground = new SolidColorBrush(Color.FromRgb(244, 247, 251)),
            BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124))
        };
        ApplyDarkComboBoxItems(_dataSourceBox);
        foreach (var source in new[]
        {
            "", "Pv1Power", "Pv1Voltage", "Pv1Current",
            "Pv2Power", "Pv2Voltage", "Pv2Current", "InverterPower"
        })
            _dataSourceBox.Items.Add(source);
        _dataSourceBox.SelectedItem = element.DataSource;
        fields.Children.Add(_dataSourceBox);

        fields.Children.Add(new TextBlock
        {
            Text = "Привязки к функциям и дополнительным датчикам будут добавлены в этом окне на следующем этапе.",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 14, 0, 0),
            Foreground = new SolidColorBrush(Color.FromRgb(150, 174, 197))
        });

        root.Children.Add(new ScrollViewer
        {
            Content = fields,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
        });
        Content = root;
    }

    private static void AddField(Panel parent, string label, out TextBox box, double value)
    {
        parent.Children.Add(new TextBlock
        {
            Text = label,
            Margin = new Thickness(0, 8, 0, 4),
            Foreground = new SolidColorBrush(Color.FromRgb(205, 216, 228))
        });
        box = new TextBox
        {
            Text = value.ToString("0.##", CultureInfo.InvariantCulture),
            Height = 30,
            Padding = new Thickness(7, 4, 7, 4),
            Background = new SolidColorBrush(Color.FromRgb(36, 54, 74)),
            Foreground = new SolidColorBrush(Color.FromRgb(244, 247, 251)),
            BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124)),
            CaretBrush = Brushes.White
        };
        parent.Children.Add(box);
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
            Foreground = new SolidColorBrush(Color.FromRgb(23, 32, 45)),
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
            Foreground = new SolidColorBrush(Color.FromRgb(205, 216, 228))
        });
        _bindingBox = new TextBox
        {
            Text = bindingKey,
            Height = 32,
            Padding = new Thickness(7, 5, 7, 5),
            Background = new SolidColorBrush(Color.FromRgb(36, 54, 74)),
            Foreground = new SolidColorBrush(Color.FromRgb(244, 247, 251)),
            BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124)),
            CaretBrush = Brushes.White
        };
        fields.Children.Add(_bindingBox);
        fields.Children.Add(new TextBlock
        {
            Text = "Поле сохраняет идентификатор привязки. Подключение к датчикам и выполнение функций будут реализованы отдельно.",
            TextWrapping = TextWrapping.Wrap,
            Margin = new Thickness(0, 12, 0, 0),
            Foreground = new SolidColorBrush(Color.FromRgb(150, 174, 197))
        });
        root.Children.Add(fields);
        Content = root;
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
            Foreground = new SolidColorBrush(Color.FromRgb(23, 32, 45)),
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
            Foreground = new SolidColorBrush(Color.FromRgb(244, 247, 251)),
            BorderBrush = new SolidColorBrush(Color.FromRgb(73, 98, 124)),
            CaretBrush = Brushes.White,
            SelectionBrush = new SolidColorBrush(Color.FromRgb(65, 112, 158))
        };
        root.Children.Add(_textBox);
        Content = root;
        Loaded += (_, _) =>
        {
            _textBox.Focus();
            _textBox.SelectAll();
        };
    }
}
