using System.ComponentModel.DataAnnotations;

namespace CoreWiki.Models;

// One immutable row per save: a wiki's edit history is only worth anything if
// past revisions can never be rewritten, so nothing ever updates a row in
// this table -- WikiService only ever inserts.
public class WikiRevision
{
    public int Id { get; set; }

    public int WikiPageId { get; set; }
    public WikiPage? WikiPage { get; set; }

    [Required, MaxLength(200)]
    public string Title { get; set; } = "";

    [Required]
    public string Content { get; set; } = "";

    [MaxLength(500)]
    public string EditSummary { get; set; } = "";

    public DateTime EditedUtc { get; set; }
}
