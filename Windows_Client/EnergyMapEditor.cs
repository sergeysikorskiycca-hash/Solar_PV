using System.Globalization;
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
    public string Foreground { get; set; } = "#FFFFFFFF";
    public string Background { get; set; } = "#00000000";
    public string BorderColor { get; set; } = "#00000000";
    public double BorderWidth { get; set; }
    public double CornerRadius { get; set; }
    public bool Bold { get; set; }
    public string DataSource { get; set; } = "";

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
            var weight = (string?)textNode.Attribute("font-weight") ?? "";
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
                    Foreground = NormalizeColor(fill),
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
            return "#FF" + new string(value.Skip(1).Select(c => new string(c, 2)[0]).ToArray());
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
        _list.SelectionChanged += (_, _) => LoadFields();
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
        AddField(editor, "Цвет текста", "Foreground");
        AddField(editor, "Цвет фона", "Background");
        AddField(editor, "Цвет рамки", "BorderColor");
        AddField(editor, "Толщина рамки", "BorderWidth");
        AddField(editor, "Скругление", "CornerRadius");
        editor.Children.Add(new TextBlock { Text = "Источник данных", Margin = new Thickness(0, 8, 0, 4), Foreground = Brushes.LightGray });
        foreach (var item in new[] { "", "Pv1Power", "Pv1Voltage", "Pv1Current", "Pv2Power", "Pv2Voltage", "Pv2Current", "InverterPower", "HousePower", "GridPower", "BatteryVoltage", "BatteryCharge", "BatteryPower" })
            _dataSource.Items.Add(item);
        editor.Children.Add(_dataSource);
        editorScroll.Content = editor;
        Grid.SetColumn(editorScroll, 1);
        body.Children.Add(editorScroll);
        root.Children.Add(body);

        Content = root;
        RefreshList();
        if (_elements.Count > 0) _list.SelectedIndex = 0;
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
        parent.Children.Add(new TextBlock { Text = label, Margin = new Thickness(0, 8, 0, 4), Foreground = Brushes.LightGray });
        var box = new TextBox { Height = 28, Padding = new Thickness(6, 3, 6, 3) };
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
        if (_list.SelectedItem is not EnergyMapElement item) return;
        _fields["Text"].Text = item.Text;
        _fields["X"].Text = F(item.X);
        _fields["Y"].Text = F(item.Y);
        _fields["Width"].Text = F(item.Width);
        _fields["Height"].Text = F(item.Height);
        _fields["FontSize"].Text = F(item.FontSize);
        _fields["Foreground"].Text = item.Foreground;
        _fields["Background"].Text = item.Background;
        _fields["BorderColor"].Text = item.BorderColor;
        _fields["BorderWidth"].Text = F(item.BorderWidth);
        _fields["CornerRadius"].Text = F(item.CornerRadius);
        _dataSource.SelectedItem = item.DataSource;
    }

    private void ApplyFields()
    {
        if (_list.SelectedItem is not EnergyMapElement item) return;
        item.Text = _fields["Text"].Text;
        item.X = N(_fields["X"].Text, item.X);
        item.Y = N(_fields["Y"].Text, item.Y);
        item.Width = Math.Max(12, N(_fields["Width"].Text, item.Width));
        item.Height = Math.Max(12, N(_fields["Height"].Text, item.Height));
        item.FontSize = Math.Clamp(N(_fields["FontSize"].Text, item.FontSize), 6, 160);
        item.Foreground = _fields["Foreground"].Text;
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
