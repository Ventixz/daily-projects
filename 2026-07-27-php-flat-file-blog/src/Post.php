<?php

declare(strict_types=1);

require_once __DIR__ . '/Markdown.php';

final class Post
{
    public function __construct(
        public readonly string $slug,
        public readonly string $title,
        public readonly DateTimeImmutable $date,
        public readonly string $bodyMarkdown,
    ) {
    }

    public static function fromFile(string $path): self
    {
        $raw = file_get_contents($path);
        if ($raw === false) {
            throw new RuntimeException("Cannot read post file: {$path}");
        }

        [$frontMatter, $body] = self::splitFrontMatter($raw);
        $slug = preg_replace('/\.md$/', '', basename($path));

        return new self(
            slug: $slug,
            title: $frontMatter['title'] ?? $slug,
            date: new DateTimeImmutable($frontMatter['date'] ?? '@0'),
            bodyMarkdown: $body,
        );
    }

    public function renderedBody(): string
    {
        return Markdown::toHtml($this->bodyMarkdown);
    }

    /**
     * @return array{0: array<string, string>, 1: string}
     */
    private static function splitFrontMatter(string $raw): array
    {
        if (!preg_match('/^---\n(.*?)\n---\n?(.*)$/s', $raw, $m)) {
            return [[], trim($raw)];
        }

        $meta = [];
        foreach (explode("\n", $m[1]) as $line) {
            if (str_contains($line, ':')) {
                [$key, $value] = explode(':', $line, 2);
                $meta[trim($key)] = trim($value);
            }
        }

        return [$meta, trim($m[2])];
    }
}
