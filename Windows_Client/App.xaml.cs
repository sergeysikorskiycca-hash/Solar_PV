namespace SolarPV.Client;

public partial class App : System.Windows.Application
{
    public App()
    {
        InitializeComponent();
        Resources["SystemTextBrush"] = EnergyMapEditorSystemTheme.BrushFor("text|primary");
        Resources["SystemBackgroundBrush"] = EnergyMapEditorSystemTheme.BrushFor("background|surface");
        Resources["SystemSelectionBrush"] = EnergyMapEditorSystemTheme.BrushFor("state|selected");
        Resources[System.Windows.SystemColors.WindowBrushKey] = EnergyMapEditorSystemTheme.BrushFor("background|surface");
        Resources[System.Windows.SystemColors.WindowTextBrushKey] = EnergyMapEditorSystemTheme.BrushFor("text|primary");
        Resources[System.Windows.SystemColors.ControlBrushKey] = EnergyMapEditorSystemTheme.BrushFor("background|surface");
        Resources[System.Windows.SystemColors.ControlTextBrushKey] = EnergyMapEditorSystemTheme.BrushFor("text|primary");
        Resources[System.Windows.SystemColors.HighlightBrushKey] = EnergyMapEditorSystemTheme.BrushFor("state|selected");
        Resources[System.Windows.SystemColors.HighlightTextBrushKey] = EnergyMapEditorSystemTheme.BrushFor("text|primary");
    }
}
