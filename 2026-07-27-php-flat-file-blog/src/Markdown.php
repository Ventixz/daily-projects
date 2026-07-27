<?php

declare(strict_types=1);

final class Markdown
{
    public static function toHtml(string $markdown): string
    {
        $lines = preg_split('/\r\n|\r|\n/', trim($markdown));

        $blocks = [];
        $paragraph = [];
        $listItems = [];

        $flushParagraph = function () use (&$paragraph, &$blocks): void {
            if ($paragraph !== []) {
                $blocks[] = '<p>' . implode(' ', $paragraph) . '</p>';
                $paragraph = [];
            }
        };
        $flushList = function () use (&$listItems, &$blocks): void {
            if ($listItems !== []) {
                $blocks[] = '<ul>' . implode('', $listItems) . '</ul>';
                $listItems = [];
            }
        };

        foreach ($lines as $line) {
            $trimmed = trim($line);

            if ($trimmed === '') {
                $flushParagraph();
                $flushList();
                continue;
            }

            if (preg_match('/^(#{1,3})\s+(.*)$/', $trimmed, $m)) {
                $flushParagraph();
                $flushList();
                $level = strlen($m[1]);
                $blocks[] = "<h{$level}>" . self::inline($m[2]) . "</h{$level}>";
                continue;
            }

            if (preg_match('/^[-*]\s+(.*)$/', $trimmed, $m)) {
                $flushParagraph();
                $listItems[] = '<li>' . self::inline($m[1]) . '</li>';
                continue;
            }

            $flushList();
            $paragraph[] = self::inline($trimmed);
        }

        $flushParagraph();
        $flushList();

        return implode("\n", $blocks);
    }

    /**
     * Escapes first so raw HTML in a post can never reach the page unescaped,
     * then layers markup on top of the already-safe text.
     */
    private static function inline(string $text): string
    {
        $escaped = htmlspecialchars($text, ENT_QUOTES, 'UTF-8');

        $escaped = preg_replace('/`([^`]+)`/', '<code>$1</code>', $escaped);
        $escaped = preg_replace('/\*\*(.+?)\*\*/', '<strong>$1</strong>', $escaped);
        $escaped = preg_replace('/(?<!\*)\*([^*]+?)\*(?!\*)/', '<em>$1</em>', $escaped);
        $escaped = preg_replace('/\[(.+?)\]\((.+?)\)/', '<a href="$2">$1</a>', $escaped);

        return $escaped;
    }
}
