using System.IO;
using System.Text.Json;

namespace SolarPV.Client;

internal sealed class ClientSettings
{
    private static readonly string DataDirectory = Path.Combine(AppContext.BaseDirectory, "Data");
    private static readonly string SettingsPath = Path.Combine(DataDirectory, "settings.json");

    public string ControllerAddress { get; set; } = "http://192.168.4.1";
    public string Theme { get; set; } = "Dark";
    public string Design { get; set; } = "Digital";
    public Dictionary<string, EnergyMapPosition> EnergyMapPositions { get; set; } = new();
    public List<EnergyMapElement> EnergyMapElements { get; set; } = new();

    public static string? LoadControllerAddress()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return null;
            var settings = JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath));
            return string.IsNullOrWhiteSpace(settings?.ControllerAddress) ? null : settings.ControllerAddress;
        }
        catch { return null; }
    }

    public static string? LoadTheme()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return null;
            var settings = JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath));
            return string.IsNullOrWhiteSpace(settings?.Theme) ? null : settings.Theme;
        }
        catch { return null; }
    }

    public static string? LoadDesign()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return null;
            var settings = JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath));
            return string.IsNullOrWhiteSpace(settings?.Design) ? null : settings.Design;
        }
        catch { return null; }
    }

    public static Dictionary<string, EnergyMapPosition> LoadEnergyMapPositions()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return new Dictionary<string, EnergyMapPosition>();
            var settings = JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath));
            return settings?.EnergyMapPositions ?? new Dictionary<string, EnergyMapPosition>();
        }
        catch { return new Dictionary<string, EnergyMapPosition>(); }
    }


    public static List<EnergyMapElement> LoadEnergyMapElements()
    {
        try
        {
            if (!File.Exists(SettingsPath)) return new List<EnergyMapElement>();
            var settings = JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath));
            return settings?.EnergyMapElements ?? new List<EnergyMapElement>();
        }
        catch { return new List<EnergyMapElement>(); }
    }

    public static void SaveEnergyMapElements(List<EnergyMapElement> elements)
    {
        try
        {
            Directory.CreateDirectory(DataDirectory);
            var current = File.Exists(SettingsPath)
                ? JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath)) ?? new ClientSettings()
                : new ClientSettings();
            current.EnergyMapElements = elements;
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(current, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch
        {
            // Energy map settings must not interrupt monitoring.
        }
    }

    public static void SaveEnergyMapPositions(Dictionary<string, EnergyMapPosition> positions)
    {
        try
        {
            Directory.CreateDirectory(DataDirectory);
            var current = File.Exists(SettingsPath)
                ? JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath)) ?? new ClientSettings()
                : new ClientSettings();
            current.EnergyMapPositions = positions;
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(current, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch
        {
            // Layout settings must not interrupt monitoring.
        }
    }

    public static void SaveDesign(string design)
    {
        try
        {
            Directory.CreateDirectory(DataDirectory);
            var current = File.Exists(SettingsPath)
                ? JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath)) ?? new ClientSettings()
                : new ClientSettings();
            current.Design = design;
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(current, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch
        {
            // Settings failures must not interrupt monitoring.
        }
    }

    public static void SaveTheme(string theme)
    {
        try
        {
            Directory.CreateDirectory(DataDirectory);
            var current = File.Exists(SettingsPath)
                ? JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath)) ?? new ClientSettings()
                : new ClientSettings();
            current.Theme = theme;
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(current, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch
        {
            // Settings failures must not interrupt monitoring.
        }
    }

    public static void SaveControllerAddress(string address)
    {
        try
        {
            Directory.CreateDirectory(DataDirectory);
            var current = File.Exists(SettingsPath)
                ? JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath)) ?? new ClientSettings()
                : new ClientSettings();
            current.ControllerAddress = address;
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(current, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch
        {
            // Settings failures must not interrupt monitoring.
        }
    }
}
internal sealed class EnergyMapPosition
{
    public double X { get; set; }
    public double Y { get; set; }
}
