<?php

declare(strict_types=1);

namespace PhGrep;

/**
 * The outcome of searching one source: every line, which ones were
 * selected (matched, or "didn't match" under -v), and the merged
 * before/after-context blocks to print.
 */
final class SearchResult
{
    /**
     * @param string[] $lines 0-indexed, no trailing newlines
     * @param array<int,true> $selected 0-indexed line numbers that count as hits
     * @param array<int,array{0:int,1:int}> $blocks merged [start,end] ranges, 0-indexed inclusive
     */
    public function __construct(
        private readonly array $lines,
        private readonly array $selected,
        private readonly array $blocks,
    ) {
    }

    public function matchCount(): int
    {
        return count($this->selected);
    }

    public function isSelected(int $index): bool
    {
        return isset($this->selected[$index]);
    }

    public function lineAt(int $index): string
    {
        return $this->lines[$index];
    }

    /** @return array<int,array{0:int,1:int}> */
    public function blocks(): array
    {
        return $this->blocks;
    }
}
