using CoreWiki.Models;
using Microsoft.EntityFrameworkCore;

namespace CoreWiki.Data;

public class WikiDbContext(DbContextOptions<WikiDbContext> options) : DbContext(options)
{
    public DbSet<WikiPage> Pages => Set<WikiPage>();
    public DbSet<WikiRevision> Revisions => Set<WikiRevision>();

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<WikiPage>()
            .HasIndex(p => p.Slug)
            .IsUnique();

        modelBuilder.Entity<WikiRevision>()
            .HasOne(r => r.WikiPage)
            .WithMany(p => p.Revisions)
            .HasForeignKey(r => r.WikiPageId)
            .OnDelete(DeleteBehavior.Cascade);
    }
}
