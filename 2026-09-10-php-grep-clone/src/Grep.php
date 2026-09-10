<?php

declare(strict_types=1);

namespace PhGrep;

/**
 * The search itself, kept separate from both matching (PatternMatcher)
 * and printing (Formatter). Its one non-obvious job is turning a set of
 * matched line numbers plus -A/-B/-C sizes into merged context ranges,
 * the same way real grep decides when to print a "--" separator versus
 * joining two hits' context into one unbroken block.
 */
final class Grep
{
    public function __construct(
        private readonly PatternMatcher $matcher,
        private readonly Options $options,
    ) {
    }

    /**
     * @param string[] $lines
     */
    public function search(array $lines): SearchResult
    {
        $selected = [];
        foreach ($lines as $index => $line) {
            $isMatch = $this->matcher->matches($line);
            if ($isMatch !== $this->options->invertMatch) {
                $selected[$index] = true;
            }
        }

        $blocks = $this->buildBlocks(array_keys($selected), count($lines));

        return new SearchResult($lines, $selected, $blocks);
    }

    /**
     * @param int[] $selectedIndices
     * @return array<int,array{0:int,1:int}>
     */
    private function buildBlocks(array $selectedIndices, int $lineCount): array
    {
        if ($selectedIndices === []) {
            return [];
        }

        $before = $this->options->before;
        $after = $this->options->after;

        $ranges = [];
        foreach ($selectedIndices as $index) {
            $ranges[] = [
                max(0, $index - $before),
                min($lineCount - 1, $index + $after),
            ];
        }
        usort($ranges, static fn (array $a, array $b) => $a[0] <=> $b[0]);

        $merged = [];
        foreach ($ranges as [$start, $end]) {
            $last = count($merged) - 1;
            // "<= end + 1", not "<= end": adjacent ranges with no gap
            // between them (e.g. [0,2] and [3,5]) merge into one block too,
            // matching GNU grep's rule that a "--" separator only appears
            // where at least one line was actually skipped.
            if ($last >= 0 && $start <= $merged[$last][1] + 1) {
                $merged[$last][1] = max($merged[$last][1], $end);
            } else {
                $merged[] = [$start, $end];
            }
        }

        return $merged;
    }
}
