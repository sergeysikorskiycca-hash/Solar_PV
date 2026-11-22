using System.IO;
using System.Text.Json;

namespace SolarPV.Client;

internal sealed class ClientSettings
{
    private static readonly string DataDirectory = Path.Combine(AppContext.BaseDirectory, "Data");
    private static readonly string SettingsPath = Path.Combine(DataDirectory, "settings.json");

    public string ControllerAddress { get; set; } = "http://192.168.4.1";

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

    public static void SaveControllerAddress(string address)
    {
        try
        {
            Directory.CreateDirectory(DataDirectory);
            var temporaryPath = SettingsPath + ".tmp";
            File.WriteAllText(temporaryPath, JsonSerializer.Serialize(
                new ClientSettings { ControllerAddress = address },
                new JsonSerializerOptions { WriteIndented = true }));
            File.Move(temporaryPath, SettingsPath, true);
        }
        catch
        {
            // Settings failures must not interrupt monitoring.
        }
    }
}