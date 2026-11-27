using System.Text.Json;
namespace OllamaControl;
internal sealed class AppSettings
{
    public string ApiUrl { get; set; } = "http://localhost:11434";
    public string KeepAlive { get; set; } = "24h";
    public string TextPrimary { get; set; } = "#FFFFFF";
    public string WindowBackground { get; set; } = "#303030";
    public string PanelBackground { get; set; } = "#383838";
    public string InputBackground { get; set; } = "#252525";\n    public string ModelListBackground { get; set; } = "#252525";\n    public string SelectedModelBackground { get; set; } = "#202020";
    public string ButtonBackground { get; set; } = "#454545";
    public string ButtonBorderColor { get; set; } = "#707070";
    public string TextBackground { get; set; } = "#202020";
    public int[] CustomColors { get; set; } = Array.Empty<int>();
    private static string Dir => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "OllamaControl");
    public static AppSettings Load()
    {
        try { var p=Path.Combine(Dir,"settings.json"); if(File.Exists(p)) return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(p)) ?? new(); } catch {}
        return new();
    }
    public void Save()
    {
        Directory.CreateDirectory(Dir);
        File.WriteAllText(Path.Combine(Dir,"settings.json"),JsonSerializer.Serialize(this,new JsonSerializerOptions{WriteIndented=true}));
    }
}