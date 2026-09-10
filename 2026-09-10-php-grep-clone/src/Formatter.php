<?php

declare(strict_types=1);

namespace PhGrep;

/**
 * Renders a SearchResult into printable lines. Knows nothing about
 * files or streams — it takes a label and a result, and hands back
 * strings, so tests can check output without capturing STDOUT.
 */
final class Formatter
{
    public function __construct(private readonly Options $options)
    {
    }

    /**
     * @return string[] lines to print, no trailing newlines
     */
    public function format(string $label, bool $showLabel, SearchResult $result): array
    {
        if ($this->options->countOnly) {
            $count = (string) $result->matchCount();
            return [$showLabel ? "$label:$count" : $count];
        }

        if ($this->options->filesWithMatches) {
            return $result->matchCount() > 0 ? [$label] : [];
        }

        $out = [];
        foreach ($result->blocks() as $blockIndex => [$start, $end]) {
            if ($blockIndex > 0) {
                $out[] = '--';
            }
            for ($i = $start; $i <= $end; $i++) {
                // GNU grep's convention: ':' separates a matched line's
                // filename/number from its text, '-' separates a context
                // line's. Same separator drives both prefixes at once.
                $sep = $result->isSelected($i) ? ':' : '-';
                $prefix = $showLabel ? $label . $sep : '';
                if ($this->options->lineNumber) {
                    $prefix .= ($i + 1) . $sep;
                }
                $out[] = $prefix . $result->lineAt($i);
            }
        }
        return $out;
    }
}
