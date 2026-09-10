<?php

declare(strict_types=1);

require_once __DIR__ . '/../src/Options.php';
require_once __DIR__ . '/../src/PatternMatcher.php';
require_once __DIR__ . '/../src/SearchResult.php';
require_once __DIR__ . '/../src/Grep.php';

use PhGrep\Grep;
use PhGrep\Options;
use PhGrep\PatternMatcher;

function makeGrep(string $pattern, array $overrides = []): Grep
{
    $matcher = new PatternMatcher(
        $pattern,
        $overrides['ignoreCase'] ?? false,
        $overrides['wholeWord'] ?? false,
        $overrides['fixedString'] ?? false,
    );
    $options = new Options(
        pattern: $pattern,
        paths: [],
        invertMatch: $overrides['invertMatch'] ?? false,
        before: $overrides['before'] ?? 0,
        after: $overrides['after'] ?? 0,
    );
    return new Grep($matcher, $options);
}

return [
    'no context: one block per match, unmerged' => function () {
        $grep = makeGrep('x');
        $result = $grep->search(['x', 'a', 'x', 'a', 'x']);
        assertEquals(3, $result->matchCount());
        // Each match is its own zero-width block with no context requested.
        assertEquals([[0, 0], [2, 2], [4, 4]], $result->blocks());
    },

    'adjacent context ranges merge into a single block' => function () {
        // Matches at 0 and 3 with after=2 -> ranges [0,2] and [3,5] touch
        // exactly (end+1 == next start), so they must merge into [0,5]
        // with no "--" separator, the same way GNU grep does.
        $grep = makeGrep('x', ['after' => 2]);
        $lines = ['x', 'a', 'a', 'x', 'a', 'a'];
        $result = $grep->search($lines);
        assertEquals([[0, 5]], $result->blocks());
    },

    'distant matches stay in separate blocks' => function () {
        $grep = makeGrep('x', ['before' => 1, 'after' => 1]);
        $lines = ['x', 'a', 'a', 'a', 'a', 'x'];
        $result = $grep->search($lines);
        assertEquals([[0, 1], [4, 5]], $result->blocks());
    },

    'context is clamped to file bounds' => function () {
        $grep = makeGrep('x', ['before' => 5, 'after' => 5]);
        $result = $grep->search(['x']);
        assertEquals([[0, 0]], $result->blocks());
    },

    'invert match selects non-matching lines' => function () {
        $grep = makeGrep('x', ['invertMatch' => true]);
        $result = $grep->search(['x', 'a', 'x', 'b']);
        assertEquals(2, $result->matchCount());
        assertTrue($result->isSelected(1));
        assertTrue($result->isSelected(3));
        assertFalse($result->isSelected(0));
    },

    'no matches produces no blocks' => function () {
        $grep = makeGrep('zzz');
        $result = $grep->search(['a', 'b', 'c']);
        assertEquals(0, $result->matchCount());
        assertEquals([], $result->blocks());
    },
];
