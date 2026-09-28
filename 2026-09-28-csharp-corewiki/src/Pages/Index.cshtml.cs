using CoreWiki.Models;
using CoreWiki.Services;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace CoreWiki.Pages;

public class IndexModel(WikiService wiki) : PageModel
{
    public List<WikiPage> Pages { get; private set; } = [];

    public async Task OnGetAsync()
    {
        Pages = await wiki.GetAllAsync();
    }
}
