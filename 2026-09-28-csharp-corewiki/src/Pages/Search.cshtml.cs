using CoreWiki.Models;
using CoreWiki.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace CoreWiki.Pages;

public class SearchModel(WikiService wiki) : PageModel
{
    [BindProperty(SupportsGet = true)]
    public string Q { get; set; } = "";

    public List<WikiPage> Results { get; private set; } = [];

    public async Task OnGetAsync()
    {
        Results = await wiki.SearchAsync(Q);
    }
}
