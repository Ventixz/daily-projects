using System.Text.RegularExpressions;
using CoreWiki.Data;
using CoreWiki.Models;
using Microsoft.EntityFrameworkCore;

namespace CoreWiki.Services;

public enum SaveOutcome { Created, Updated, Conflict }

public record SaveResult(SaveOutcome Outcome, WikiPage Page);

public partial class WikiService(WikiDbContext db)
{
    public Task<List<WikiPage>> GetAllAsync() =>
        db.Pages.AsNoTracking().OrderBy(p => p.Title).ToListAsync();

    public Task<WikiPage?> FindBySlugAsync(string slug) =>
        db.Pages.AsNoTracking().FirstOrDefaultAsync(p => p.Slug == slug);

    public Task<List<WikiRevision>> GetHistoryAsync(int wikiPageId) =>
        db.Revisions.AsNoTracking()
            .Where(r => r.WikiPageId == wikiPageId)
            .OrderByDescending(r => r.EditedUtc)
            .ToListAsync();

    public Task<WikiRevision?> FindRevisionAsync(int revisionId) =>
        db.Revisions.AsNoTracking().FirstOrDefaultAsync(r => r.Id == revisionId);

    public async Task<List<WikiPage>> SearchAsync(string query)
    {
        if (string.IsNullOrWhiteSpace(query))
        {
            return [];
        }

        // EF.Functions.Like translates to SQL LIKE, which SQLite matches
        // case-insensitively for ASCII by default -- a plain .Contains() call
        // here would instead pull every page into memory to run a
        // culture-aware C# comparison.
        var pattern = $"%{query}%";
        return await db.Pages.AsNoTracking()
            .Where(p => EF.Functions.Like(p.Title, pattern) || EF.Functions.Like(p.Content, pattern))
            .OrderBy(p => p.Title)
            .ToListAsync();
    }

    /// <summary>
    /// Creates a page (slugIfExisting is null) or updates one, always appending
    /// a revision. expectedVersion is the Version the editor loaded the form
    /// with; a mismatch at save time means someone else saved in between, and
    /// this returns Conflict instead of overwriting their edit.
    /// </summary>
    public async Task<SaveResult> SaveAsync(string? slugIfExisting, string title, string content, string editSummary, int expectedVersion)
    {
        var now = DateTime.UtcNow;

        if (slugIfExisting is null)
        {
            var page = new WikiPage
            {
                Slug = await UniqueSlugAsync(Slugify(title)),
                Title = title,
                Content = content,
                Version = 1,
                CreatedUtc = now,
                UpdatedUtc = now,
            };
            page.Revisions.Add(new WikiRevision
            {
                Title = title,
                Content = content,
                EditSummary = string.IsNullOrWhiteSpace(editSummary) ? "Created page" : editSummary,
                EditedUtc = now,
            });

            db.Pages.Add(page);
            await db.SaveChangesAsync();
            return new SaveResult(SaveOutcome.Created, page);
        }

        var existing = await db.Pages.FirstOrDefaultAsync(p => p.Slug == slugIfExisting)
            ?? throw new InvalidOperationException($"No page with slug '{slugIfExisting}'.");

        db.Entry(existing).Property(p => p.Version).OriginalValue = expectedVersion;
        existing.Title = title;
        existing.Content = content;
        existing.UpdatedUtc = now;
        existing.Version = expectedVersion + 1;
        db.Revisions.Add(new WikiRevision
        {
            WikiPageId = existing.Id,
            Title = title,
            Content = content,
            EditSummary = editSummary,
            EditedUtc = now,
        });

        try
        {
            await db.SaveChangesAsync();
            return new SaveResult(SaveOutcome.Updated, existing);
        }
        catch (DbUpdateConcurrencyException)
        {
            db.Entry(existing).State = EntityState.Detached;
            var current = await db.Pages.AsNoTracking().FirstAsync(p => p.Slug == slugIfExisting);
            return new SaveResult(SaveOutcome.Conflict, current);
        }
    }

    public static string Slugify(string title)
    {
        var lowered = title.Trim().ToLowerInvariant();
        var hyphenated = NonSlugCharacters().Replace(lowered, "-");
        return hyphenated.Trim('-') is { Length: > 0 } slug ? slug : "page";
    }

    private async Task<string> UniqueSlugAsync(string baseSlug)
    {
        var slug = baseSlug;
        var suffix = 2;
        while (await db.Pages.AnyAsync(p => p.Slug == slug))
        {
            slug = $"{baseSlug}-{suffix++}";
        }
        return slug;
    }

    [GeneratedRegex("[^a-z0-9]+")]
    private static partial Regex NonSlugCharacters();
}
