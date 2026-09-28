using System.ComponentModel.DataAnnotations;
using CoreWiki.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace CoreWiki.Pages.Wiki;

public class EditModel(WikiService wiki) : PageModel
{
    [BindProperty(SupportsGet = true)]
    public string Slug { get; set; } = "";

    [BindProperty]
    [Required, MaxLength(200)]
    public string Title { get; set; } = "";

    [BindProperty]
    public string Body { get; set; } = "";

    [BindProperty]
    public string EditSummary { get; set; } = "";

    [BindProperty]
    public int Version { get; set; }

    public string? ConflictMessage { get; private set; }

    public async Task<IActionResult> OnGetAsync()
    {
        var page = await wiki.FindBySlugAsync(Slug);
        if (page is null)
        {
            return NotFound();
        }

        Title = page.Title;
        Body = page.Content;
        Version = page.Version;
        return Page();
    }

    public async Task<IActionResult> OnPostAsync()
    {
        if (!ModelState.IsValid)
        {
            return Page();
        }

        var result = await wiki.SaveAsync(Slug, Title, Body, EditSummary, Version);

        if (result.Outcome == SaveOutcome.Conflict)
        {
            // Don't silently overwrite: show the other editor's version of
            // the fields the form was about to submit over, and make the
            // reader re-apply their own change on top of it.
            ConflictMessage = "Someone else saved this page while you were editing. " +
                "Their version is now shown below -- re-apply your change and save again.";
            Title = result.Page.Title;
            Body = result.Page.Content;
            Version = result.Page.Version;
            return Page();
        }

        return RedirectToPage("/Wiki/View", new { slug = result.Page.Slug });
    }
}
