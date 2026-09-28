using CoreWiki.Data;
using CoreWiki.Services;
using Microsoft.EntityFrameworkCore;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddRazorPages();

var connectionString = builder.Configuration.GetConnectionString("Wiki") ?? "Data Source=wiki.db";
builder.Services.AddDbContext<WikiDbContext>(options => options.UseSqlite(connectionString));
builder.Services.AddScoped<WikiService>();

var app = builder.Build();

using (var scope = app.Services.CreateScope())
{
    var db = scope.ServiceProvider.GetRequiredService<WikiDbContext>();
    db.Database.Migrate();
    await SeedData.EnsureHomePageAsync(db);
}

if (!app.Environment.IsDevelopment())
{
    app.UseExceptionHandler("/Error");
}
app.UseStaticFiles();

app.UseRouting();

app.UseAuthorization();

app.MapRazorPages();

app.Run();

public partial class Program;
