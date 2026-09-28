using CoreWiki.Data;
using Microsoft.Data.Sqlite;
using Microsoft.EntityFrameworkCore;

namespace CoreWiki.Tests;

/// <summary>
/// A shared in-memory SQLite database, kept alive by one open connection.
/// Real SQLite (not EF's InMemory provider) is used so the concurrency-token
/// test below exercises the same WHERE-clause-based conflict detection the
/// app relies on in production. Each call to CreateContext() opens a fresh
/// WikiDbContext over the same connection, so two contexts can each hold
/// their own tracked snapshot of a row -- exactly like two browser tabs
/// editing the same wiki page.
/// </summary>
public sealed class SharedSqliteDatabase : IDisposable
{
    private readonly SqliteConnection _connection;

    public SharedSqliteDatabase()
    {
        _connection = new SqliteConnection("Data Source=:memory:");
        _connection.Open();
        using var context = CreateContext();
        context.Database.EnsureCreated();
    }

    public WikiDbContext CreateContext()
    {
        var options = new DbContextOptionsBuilder<WikiDbContext>()
            .UseSqlite(_connection)
            .Options;
        return new WikiDbContext(options);
    }

    public void Dispose() => _connection.Dispose();
}
