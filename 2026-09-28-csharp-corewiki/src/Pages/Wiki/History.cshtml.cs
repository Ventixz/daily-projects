using CoreWiki.Models;
using CoreWiki.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace CoreWiki.Pages.Wiki;

public class HistoryModel(WikiService wiki) : PageModel
{
    [BindProperty(SupportsGet = true)]
    public string Slug { get; set; } = "";

    public WikiPage? WikiPage { get; private set; }
    public List<WikiRevision> Revisions { get; private set; } = [];

    public async Task<IActionResult> OnGetAsync()
    {
        WikiPage = await wiki.FindBySlugAsync(Slug);
        if (WikiPage is null)
        {
            return NotFound();
        }

        Revisions = await wiki.GetHistoryAsync(WikiPage.Id);
        return Page();
    }
}
