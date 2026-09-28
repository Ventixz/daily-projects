using CoreWiki.Models;
using CoreWiki.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace CoreWiki.Pages.Wiki;

public class ViewModel(WikiService wiki) : PageModel
{
    [BindProperty(SupportsGet = true)]
    public string Slug { get; set; } = "";

    public WikiPage? WikiPage { get; private set; }
    public string? RenderedHtml { get; private set; }

    public async Task OnGetAsync()
    {
        WikiPage = await wiki.FindBySlugAsync(Slug);
        if (WikiPage is not null)
        {
            RenderedHtml = MarkdownRenderer.ToHtml(WikiPage.Content);
        }
    }
}
