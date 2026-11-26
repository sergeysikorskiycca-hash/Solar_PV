using System.Diagnostics;

namespace OllamaControl;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        EnsureDesktopShortcut();
        Application.Run(new MainForm());
    }

    private static void EnsureDesktopShortcut()
    {
        try
        {
            var exePath = Environment.ProcessPath;
            if (string.IsNullOrWhiteSpace(exePath) || !File.Exists(exePath))
                return;

            var desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
            if (string.IsNullOrWhiteSpace(desktop))
                return;

            var shortcutPath = Path.Combine(desktop, "Ollama Control.lnk");
            if (File.Exists(shortcutPath))
                return;

            var shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType is null)
                return;

            dynamic shell = Activator.CreateInstance(shellType)!;
            dynamic shortcut = shell.CreateShortcut(shortcutPath);
            shortcut.TargetPath = exePath;
            shortcut.WorkingDirectory = AppContext.BaseDirectory;
            shortcut.Description = "Управление локальной Ollama";
            shortcut.IconLocation = exePath + ",0";
            shortcut.Save();
        }
        catch
        {
            // Ошибка создания ярлыка не должна мешать запуску программы.
        }
    }
}
