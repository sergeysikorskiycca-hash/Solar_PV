using System.Diagnostics;
using System.Drawing;
namespace OllamaControl;
internal sealed class MainForm : Form
{
    private readonly AppSettings cfg=AppSettings.Load();
    private readonly OllamaClient api=new();
    private readonly TextBox address=new(){Width=220};
    private readonly TextBox keep=new(){Width=70};
    private readonly ComboBox models=new(){Width=280,DropDownStyle=ComboBoxStyle.DropDownList};
    private readonly ListBox running=new(){Width=280,Height=90};
    private readonly TextBox log=new(){Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Vertical,Dock=DockStyle.Fill};
    private readonly Label status=new(){AutoSize=true,Text="Не подключено"};
    public MainForm()
    {
        Text="Ollama Control"; Width=780; Height=560; MinimumSize=new Size(680,460); StartPosition=FormStartPosition.CenterScreen;
        address.Text=cfg.ApiUrl; keep.Text=cfg.KeepAlive; BuildUi(); Theme(); Shown+=async(_,_)=>await RefreshAll();
    }
    private void BuildUi()
    {
        var root=new TableLayoutPanel{Dock=DockStyle.Fill,Padding=new Padding(12),ColumnCount=1,RowCount=4};
        root.RowStyles.Add(new RowStyle(SizeType.AutoSize));root.RowStyles.Add(new RowStyle(SizeType.AutoSize));root.RowStyles.Add(new RowStyle(SizeType.AutoSize));root.RowStyles.Add(new RowStyle(SizeType.Percent,100));Controls.Add(root);
        var top=new FlowLayoutPanel{AutoSize=true,Dock=DockStyle.Fill};
        top.Controls.Add(new Label{Text="Адрес Ollama:",AutoSize=true,Padding=new Padding(0,7,0,0)});top.Controls.Add(address);top.Controls.Add(Btn("Проверить",RefreshAll));top.Controls.Add(status);root.Controls.Add(top);
        var actions=new FlowLayoutPanel{AutoSize=true,Dock=DockStyle.Fill,Padding=new Padding(0,10,0,8)};
        actions.Controls.Add(new Label{Text="Модель:",AutoSize=true,Padding=new Padding(0,7,0,0)});actions.Controls.Add(models);
        actions.Controls.Add(Btn("Обновить списки",RefreshAll));actions.Controls.Add(new Label{Text="Время:",AutoSize=true,Padding=new Padding(8,7,0,0)});actions.Controls.Add(keep);
        actions.Controls.Add(Btn("Загрузить",LoadSelected));actions.Controls.Add(Btn("Выгрузить",StopSelected));actions.Controls.Add(Btn("Выгрузить все",StopAll));
        actions.Controls.Add(Btn("OpenCode",LaunchOpenCode));actions.Controls.Add(Btn("Цвета",ChooseColors));actions.Controls.Add(Btn("Проверить обновления",()=>UpdateManager.CheckAsync(this)));root.Controls.Add(actions);
        var mid=new FlowLayoutPanel{AutoSize=true,Dock=DockStyle.Fill};mid.Controls.Add(new Label{Text="Модели в памяти:",AutoSize=true});mid.Controls.Add(running);root.Controls.Add(mid);root.Controls.Add(log);
    }
    private Button Btn(string caption,Func<Task> action){var b=new Button{Text=caption,AutoSize=true,Padding=new Padding(7,5,7,5)};b.Click+=async(_,_)=>{try{await action();}catch(Exception e){Write("Ошибка: "+e.Message);}};return b;}
    private void Theme()
    {
        BackColor=ColorTranslator.FromHtml(cfg.WindowBackground);ForeColor=ColorTranslator.FromHtml(cfg.TextPrimary);
        ApplyTheme(this);log.BackColor=ColorTranslator.FromHtml(cfg.LogBackground);log.ForeColor=ColorTranslator.FromHtml(cfg.TextPrimary);
        address.BackColor=keep.BackColor=ColorTranslator.FromHtml(cfg.InputBackground);address.ForeColor=keep.ForeColor=ColorTranslator.FromHtml(cfg.TextPrimary);
    }
    private void ApplyTheme(Control parent){foreach(Control c in parent.Controls){c.ForeColor=ColorTranslator.FromHtml(cfg.TextPrimary);c.BackColor=ColorTranslator.FromHtml(c is Button?cfg.ButtonBackground:c is TextBox||c is ComboBox||c is ListBox?cfg.InputBackground:cfg.PanelBackground);ApplyTheme(c);}}
    private async Task RefreshAll()
    {
        api.BaseUrl=address.Text.Trim();cfg.ApiUrl=api.BaseUrl;cfg.KeepAlive=keep.Text.Trim();cfg.Save();
        var v=await api.VersionAsync();var m=await api.ModelsAsync("/api/tags");var r=await api.ModelsAsync("/api/ps");
        models.Items.Clear();models.Items.AddRange(m.Cast<object>().ToArray());if(models.Items.Count>0)models.SelectedIndex=0;
        running.Items.Clear();running.Items.AddRange(r.Cast<object>().ToArray());status.Text="Ollama "+v+" · подключено";Write("Моделей: "+m.Count+"; в памяти: "+r.Count);
    }
    private string Selected=>models.SelectedItem?.ToString()??"";
    private async Task LoadSelected(){if(Selected.Length==0)return;await api.LoadAsync(Selected,keep.Text.Trim());Write("Загрузка: "+Selected);await RefreshRunning();}
    private async Task StopSelected(){if(Selected.Length==0)return;await api.StopAsync(Selected);Write("Выгрузка: "+Selected);await RefreshRunning();}
    private async Task StopAll(){if(MessageBox.Show("Выгрузить все модели из памяти?","Подтверждение",MessageBoxButtons.YesNo)!=DialogResult.Yes)return;foreach(var m in running.Items.Cast<string>().ToArray())await api.StopAsync(m);Write("Отправлены команды выгрузки.");await RefreshRunning();}
    private async Task RefreshRunning(){var r=await api.ModelsAsync("/api/ps");running.Items.Clear();running.Items.AddRange(r.Cast<object>().ToArray());}
    private Task LaunchOpenCode(){Process.Start(new ProcessStartInfo("opencode"){UseShellExecute=true});Write("Запуск OpenCode.");return Task.CompletedTask;}
    private Task ChooseColors()
    {
        var choices=new (string,Func<string>,Action<string>)[]{("Текст",()=>cfg.TextPrimary,v=>cfg.TextPrimary=v),("Вторичный текст",()=>cfg.TextSecondary,v=>cfg.TextSecondary=v),("Окно",()=>cfg.WindowBackground,v=>cfg.WindowBackground=v),("Панели",()=>cfg.PanelBackground,v=>cfg.PanelBackground=v),("Поля",()=>cfg.InputBackground,v=>cfg.InputBackground=v),("Кнопки",()=>cfg.ButtonBackground,v=>cfg.ButtonBackground=v),("Журнал",()=>cfg.LogBackground,v=>cfg.LogBackground=v)};
        foreach(var x in choices){using var d=new ColorDialog{Color=ColorTranslator.FromHtml(x.Item2())};if(d.ShowDialog(this)==DialogResult.OK)x.Item3("#"+d.Color.R.ToString("X2")+d.Color.G.ToString("X2")+d.Color.B.ToString("X2"));}
        cfg.Save();Theme();return Task.CompletedTask;
    }
    private void Write(string s)=>log.AppendText("["+DateTime.Now.ToString("HH:mm:ss")+"] "+s+Environment.NewLine);
}