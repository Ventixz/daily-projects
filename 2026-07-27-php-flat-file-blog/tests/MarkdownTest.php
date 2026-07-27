<?php

declare(strict_types=1);

return [
    'headers render as h1/h2/h3' => function (): void {
        $html = Markdown::toHtml("# Title\n\n## Subtitle\n\n### Small");
        assertStringContains('<h1>Title</h1>', $html);
        assertStringContains('<h2>Subtitle</h2>', $html);
        assertStringContains('<h3>Small</h3>', $html);
    },

    'wrapped lines in one paragraph join with a space' => function (): void {
        $html = Markdown::toHtml("This is\na single paragraph.");
        assertEquals('<p>This is a single paragraph.</p>', $html);
    },

    'a blank line starts a new paragraph' => function (): void {
        $html = Markdown::toHtml("First.\n\nSecond.");
        assertEquals("<p>First.</p>\n<p>Second.</p>", $html);
    },

    'bold, italic, code and links render as nested inline markup' => function (): void {
        $html = Markdown::toHtml('**bold** and *italic* and `code` and [link](https://example.com)');
        assertStringContains('<strong>bold</strong>', $html);
        assertStringContains('<em>italic</em>', $html);
        assertStringContains('<code>code</code>', $html);
        assertStringContains('<a href="https://example.com">link</a>', $html);
    },

    'list items become a single ul/li block' => function (): void {
        $html = Markdown::toHtml("- first\n- second");
        assertEquals('<ul><li>first</li><li>second</li></ul>', $html);
    },

    'raw HTML in source is escaped before markup is applied' => function (): void {
        $html = Markdown::toHtml('<script>alert(1)</script> **bold**');
        assertStringContains('&lt;script&gt;', $html);
        assertTrue(!str_contains($html, '<script>'), 'raw <script> tag must not survive into the output');
        assertStringContains('<strong>bold</strong>', $html);
    },
];
