namespace SolarPV.Client;

public partial class App : System.Windows.Application
{
    public App()
    {
        InitializeComponent();
        Resources["SystemTextBrush"] = EnergyMapEditorSystemTheme.BrushFor("text|primary");
        Resources["SystemBackgroundBrush"] = EnergyMapEditorSystemTheme.BrushFor("background|surface");
        Resources["SystemSelectionBrush"] = EnergyMapEditorSystemTheme.BrushFor("state|selected");
        Resources["WindowBrush"] = EnergyMapEditorSystemTheme.BrushFor("background|window");
        Resources["PanelBrush"] = EnergyMapEditorSystemTheme.BrushFor("background|panel");
    }
}
