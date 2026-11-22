using System.IO;
using System.Text.Json;

namespace SolarPV.Client;

internal sealed class ClientSettings
{
    private static readonly string DataDirectory = Path.Combine(AppContext.BaseDirectory, "Data");
    private static readonly string SettingsPath = Path.Combine(DataDirectory, "settings.json");

    public string ControllerAddress { get; set; } = "http://192.168.4.1";
    public string Theme { get; set; } = "Dark";

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

    public static string? LoadTheme()\n    {\n        try\n        {\n            if (!File.Exists(SettingsPath)) return null;\n            var settings = JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath));\n            return settings?.Theme;\n        }\n        catch { return null; }\n    }\n\n    public static void SaveTheme(string theme)\n    {\n        try\n        {\n            Directory.CreateDirectory(DataDirectory);\n            var current = File.Exists(SettingsPath)\n                ? JsonSerializer.Deserialize<ClientSettings>(File.ReadAllText(SettingsPath)) ?? new ClientSettings()\n                : new ClientSettings();\n            current.Theme = theme;\n            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(current, new JsonSerializerOptions { WriteIndented = true }));\n        }\n        catch { }\n    }\n\n    public static void SaveControllerAddress(string address)
    {
        try
        {
            Directory.CreateDirectory(DataDirectory);
            var temporaryPath = SettingsPath + ".tmp";
            File.WriteAllText(temporaryPath, JsonSerializer.Serialize(
                new ClientSettings { ControllerAddress = address, Theme = LoadTheme() ?? "Dark" },
                new JsonSerializerOptions { WriteIndented = true }));
            File.Move(temporaryPath, SettingsPath, true);
        }
        catch
        {
            // Settings failures must not interrupt monitoring.
        }
    }
}