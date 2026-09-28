using CoreWiki.Models;
using Microsoft.EntityFrameworkCore;

namespace CoreWiki.Data;

public static class SeedData
{
    public static async Task EnsureHomePageAsync(WikiDbContext db)
    {
        if (await db.Pages.AnyAsync(p => p.Slug == "home"))
        {
            return;
        }

        var now = DateTime.UtcNow;
        const string content = """
            Welcome to **CoreWiki**. This page was created automatically because
            a wiki with no Home page is a wiki nobody can find.

            Use *Edit* above to change this page, or the search box to look for
            another one.
            """;

        var home = new WikiPage
        {
            Slug = "home",
            Title = "Home",
            Content = content,
            Version = 1,
            CreatedUtc = now,
            UpdatedUtc = now,
        };
        home.Revisions.Add(new WikiRevision
        {
            Title = home.Title,
            Content = home.Content,
            EditSummary = "Initial seed",
            EditedUtc = now,
        });

        db.Pages.Add(home);
        await db.SaveChangesAsync();
    }
}
