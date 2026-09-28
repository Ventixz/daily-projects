using CoreWiki.Services;
using Xunit;

namespace CoreWiki.Tests;

public class WikiServiceTests : IDisposable
{
    private readonly SharedSqliteDatabase _db = new();

    public void Dispose() => _db.Dispose();

    [Theory]
    [InlineData("Hello, World!", "hello-world")]
    [InlineData("  leading and trailing  ", "leading-and-trailing")]
    [InlineData("Already-Slugged", "already-slugged")]
    [InlineData("!!!", "page")]
    public void Slugify_normalizes_titles_into_url_slugs(string title, string expected)
    {
        Assert.Equal(expected, WikiService.Slugify(title));
    }

    [Fact]
    public async Task SaveAsync_with_no_existing_slug_creates_a_page_and_its_first_revision()
    {
        var service = new WikiService(_db.CreateContext());

        var result = await service.SaveAsync(null, "Getting Started", "# Hi", "", 0);

        Assert.Equal(SaveOutcome.Created, result.Outcome);
        Assert.Equal("getting-started", result.Page.Slug);
        Assert.Equal(1, result.Page.Version);

        var history = await service.GetHistoryAsync(result.Page.Id);
        var revision = Assert.Single(history);
        Assert.Equal("# Hi", revision.Content);
    }

    [Fact]
    public async Task SaveAsync_disambiguates_a_slug_that_would_otherwise_collide()
    {
        var service = new WikiService(_db.CreateContext());

        await service.SaveAsync(null, "Setup", "first", "", 0);
        var second = await service.SaveAsync(null, "Setup", "second", "", 0);

        Assert.Equal("setup-2", second.Page.Slug);
    }

    [Fact]
    public async Task SaveAsync_on_an_existing_slug_updates_the_page_and_appends_a_revision()
    {
        var service = new WikiService(_db.CreateContext());
        var created = await service.SaveAsync(null, "Setup", "v1", "created", 0);

        var updated = await service.SaveAsync("setup", "Setup", "v2", "editing", created.Page.Version);

        Assert.Equal(SaveOutcome.Updated, updated.Outcome);
        Assert.Equal("v2", updated.Page.Content);
        Assert.Equal(2, updated.Page.Version);

        var history = await service.GetHistoryAsync(updated.Page.Id);
        Assert.Equal(2, history.Count);
        Assert.Equal("v2", history[0].Content); // newest first
        Assert.Equal("v1", history[1].Content);
    }

    [Fact]
    public async Task SaveAsync_rejects_a_stale_edit_instead_of_overwriting_the_newer_one()
    {
        // Two independent WikiService instances over the same underlying
        // database stand in for two editors who both opened the edit form
        // for the same page at version 1.
        var writerA = new WikiService(_db.CreateContext());
        var writerB = new WikiService(_db.CreateContext());
        var created = await writerA.SaveAsync(null, "Shared Page", "original", "", 0);
        var startingVersion = created.Page.Version;

        var firstSave = await writerA.SaveAsync("shared-page", "Shared Page", "editor A's change", "", startingVersion);
        Assert.Equal(SaveOutcome.Updated, firstSave.Outcome);

        // editor B still has the stale version number from before A saved.
        var secondSave = await writerB.SaveAsync("shared-page", "Shared Page", "editor B's change", "", startingVersion);

        Assert.Equal(SaveOutcome.Conflict, secondSave.Outcome);
        Assert.Equal("editor A's change", secondSave.Page.Content);

        var onDisk = await writerA.FindBySlugAsync("shared-page");
        Assert.Equal("editor A's change", onDisk!.Content);

        // B's rejected edit must not have left a revision behind.
        var history = await writerA.GetHistoryAsync(onDisk.Id);
        Assert.Equal(2, history.Count);
        Assert.DoesNotContain(history, r => r.Content == "editor B's change");
    }

    [Fact]
    public async Task SearchAsync_matches_title_or_content_case_insensitively()
    {
        var service = new WikiService(_db.CreateContext());
        await service.SaveAsync(null, "Rust Ownership", "borrow checker notes", "", 0);
        await service.SaveAsync(null, "Unrelated Page", "nothing to see", "", 0);

        var byTitle = await service.SearchAsync("RUST");
        var byContent = await service.SearchAsync("borrow");

        Assert.Equal(["Rust Ownership"], byTitle.Select(p => p.Title));
        Assert.Equal(["Rust Ownership"], byContent.Select(p => p.Title));
    }

    [Fact]
    public async Task SearchAsync_returns_nothing_for_a_blank_query()
    {
        var service = new WikiService(_db.CreateContext());
        await service.SaveAsync(null, "Some Page", "content", "", 0);

        var results = await service.SearchAsync("   ");

        Assert.Empty(results);
    }
}
