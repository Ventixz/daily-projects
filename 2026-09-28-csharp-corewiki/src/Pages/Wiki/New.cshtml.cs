using System.ComponentModel.DataAnnotations;
using CoreWiki.Services;
using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.RazorPages;

namespace CoreWiki.Pages.Wiki;

public class NewModel(WikiService wiki) : PageModel
{
    [BindProperty]
    [Required, MaxLength(200)]
    public string Title { get; set; } = "";

    [BindProperty]
    public string Body { get; set; } = "";

    public void OnGet(string? title)
    {
        if (!string.IsNullOrWhiteSpace(title))
        {
            Title = title;
        }
    }

    public async Task<IActionResult> OnPostAsync()
    {
        if (!ModelState.IsValid)
        {
            return Page();
        }

        var slug = WikiService.Slugify(Title);
        if (await wiki.FindBySlugAsync(slug) is not null)
        {
            ModelState.AddModelError(nameof(Title), $"A page already exists at /wiki/{slug}. Edit it instead.");
            return Page();
        }

        var result = await wiki.SaveAsync(null, Title, Body, "Created page", 0);
        return RedirectToPage("/Wiki/View", new { slug = result.Page.Slug });
    }
}
