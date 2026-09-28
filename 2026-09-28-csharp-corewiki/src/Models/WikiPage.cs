using System.ComponentModel.DataAnnotations;

namespace CoreWiki.Models;

public class WikiPage
{
    public int Id { get; set; }

    [Required, MaxLength(200)]
    public string Slug { get; set; } = "";

    [Required, MaxLength(200)]
    public string Title { get; set; } = "";

    [Required]
    public string Content { get; set; } = "";

    public DateTime CreatedUtc { get; set; }

    public DateTime UpdatedUtc { get; set; }

    // Plain int concurrency token: SQLite has no native rowversion column like
    // SQL Server's [Timestamp], so the WikiService bumps this by hand on every
    // save. EF still uses the original value in the UPDATE's WHERE clause, so
    // a stale write (someone editing from an older Version) affects zero rows
    // and raises DbUpdateConcurrencyException instead of silently clobbering
    // the other editor's change.
    [ConcurrencyCheck]
    public int Version { get; set; }

    public List<WikiRevision> Revisions { get; set; } = new();
}
