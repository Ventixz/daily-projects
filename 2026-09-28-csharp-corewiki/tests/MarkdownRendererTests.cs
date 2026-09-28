using CoreWiki.Services;
using Xunit;

namespace CoreWiki.Tests;

public class MarkdownRendererTests
{
    [Fact]
    public void ToHtml_renders_standard_markdown()
    {
        var html = MarkdownRenderer.ToHtml("# Title\n\nSome **bold** text.");

        Assert.Contains("<h1", html);
        Assert.Contains("<strong>bold</strong>", html);
    }

    [Fact]
    public void ToHtml_strips_raw_script_tags_instead_of_passing_them_through()
    {
        var html = MarkdownRenderer.ToHtml("Hello <script>alert('xss')</script> world");

        Assert.DoesNotContain("<script>", html);
    }
}
