using Markdig;

namespace CoreWiki.Services;

public static class MarkdownRenderer
{
    // DisableHtml() strips raw HTML tags from the input instead of passing
    // them through: wiki content is untrusted (anyone with edit access can
    // write it), so without this a page could embed a <script> tag and it
    // would render verbatim in every visitor's browser.
    private static readonly MarkdownPipeline Pipeline = new MarkdownPipelineBuilder()
        .UseAdvancedExtensions()
        .DisableHtml()
        .Build();

    public static string ToHtml(string markdown) => Markdown.ToHtml(markdown, Pipeline);
}
