using CoreWiki.Models;
using CoreWiki.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace CoreWiki.Pages.Wiki;

public class RevisionModel(WikiService wiki) : PageModel
{
    [BindProperty(SupportsGet = true)]
    public string Slug { get; set; } = "";

    [BindProperty(SupportsGet = true)]
    public int Id { get; set; }

    public WikiRevision? Revision { get; private set; }
    public string? RenderedHtml { get; private set; }

    public async Task<IActionResult> OnGetAsync()
    {
        Revision = await wiki.FindRevisionAsync(Id);
        if (Revision is null)
        {
            return NotFound();
        }

        RenderedHtml = MarkdownRenderer.ToHtml(Revision.Content);
        return Page();
    }
}
