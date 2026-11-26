using System.Net.Http.Json;
using System.Text.Json;
namespace OllamaControl;
internal sealed class OllamaClient
{
    private readonly HttpClient http=new();
    public string BaseUrl {get;set;}="http://localhost:11434";
    public async Task<string> VersionAsync()
    {
        using var r=await http.GetAsync(BaseUrl.TrimEnd('/')+"/api/version"); r.EnsureSuccessStatusCode();
        using var j=JsonDocument.Parse(await r.Content.ReadAsStringAsync()); return j.RootElement.GetProperty("version").GetString() ?? "?";
    }
    public async Task<List<string>> ModelsAsync(string endpoint)
    {
        using var r=await http.GetAsync(BaseUrl.TrimEnd('/')+endpoint); r.EnsureSuccessStatusCode();
        using var j=JsonDocument.Parse(await r.Content.ReadAsStringAsync());
        return j.RootElement.GetProperty("models").EnumerateArray().Select(x=>x.GetProperty("name").GetString()??"").Where(x=>x.Length>0).ToList();
    }
    public async Task LoadAsync(string model,string keepAlive)
    {
        using var r=await http.PostAsJsonAsync(BaseUrl.TrimEnd('/')+"/api/generate",new{model,prompt="",keep_alive=keepAlive}); r.EnsureSuccessStatusCode();
    }
    public async Task StopAsync(string model)
    {
        using var r=await http.PostAsJsonAsync(BaseUrl.TrimEnd('/')+"/api/generate",new{model,prompt="",keep_alive=0}); r.EnsureSuccessStatusCode();
    }
}