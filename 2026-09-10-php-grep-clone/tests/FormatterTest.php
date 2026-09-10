<?php

declare(strict_types=1);

require_once __DIR__ . '/../src/Options.php';
require_once __DIR__ . '/../src/PatternMatcher.php';
require_once __DIR__ . '/../src/SearchResult.php';
require_once __DIR__ . '/../src/Grep.php';
require_once __DIR__ . '/../src/Formatter.php';

use PhGrep\Formatter;
use PhGrep\Grep;
use PhGrep\Options;
use PhGrep\PatternMatcher;

function runFormat(string $pattern, array $lines, array $overrides = [], bool $showLabel = false, string $label = 'f.txt'): array
{
    $matcher = new PatternMatcher($pattern, false, false, false);
    $options = new Options(
        pattern: $pattern,
        paths: [],
        lineNumber: $overrides['lineNumber'] ?? false,
        countOnly: $overrides['countOnly'] ?? false,
        filesWithMatches: $overrides['filesWithMatches'] ?? false,
        before: $overrides['before'] ?? 0,
        after: $overrides['after'] ?? 0,
    );
    $result = (new Grep($matcher, $options))->search($lines);
    return (new Formatter($options))->format($label, $showLabel, $result);
}

return [
    'plain match lines, no prefix' => function () {
        $out = runFormat('b', ['a', 'b', 'c']);
        assertEquals(['b'], $out);
    },

    'line numbers use colon for matches' => function () {
        $out = runFormat('b', ['a', 'b', 'c'], ['lineNumber' => true]);
        assertEquals(['2:b'], $out);
    },

    'context lines use dash, matches use colon' => function () {
        $out = runFormat('b', ['a', 'b', 'c'], ['lineNumber' => true, 'before' => 1, 'after' => 1]);
        assertEquals(['1-a', '2:b', '3-c'], $out);
    },

    'filename prefix follows the same colon/dash rule' => function () {
        $out = runFormat('b', ['a', 'b', 'c'], ['before' => 1, 'after' => 1], showLabel: true, label: 'x.txt');
        assertEquals(['x.txt-a', 'x.txt:b', 'x.txt-c'], $out);
    },

    'separate blocks get a -- divider' => function () {
        $out = runFormat('x', ['x', 'a', 'a', 'a', 'a', 'x']);
        assertEquals(['x', '--', 'x'], $out);
    },

    'count-only mode' => function () {
        $out = runFormat('b', ['b', 'a', 'b'], ['countOnly' => true]);
        assertEquals(['2'], $out);
    },

    'count-only mode with filename prefix' => function () {
        $out = runFormat('b', ['b', 'a', 'b'], ['countOnly' => true], showLabel: true, label: 'x.txt');
        assertEquals(['x.txt:2'], $out);
    },

    'files-with-matches prints nothing when there is no match' => function () {
        $out = runFormat('zzz', ['a', 'b'], ['filesWithMatches' => true], showLabel: true, label: 'x.txt');
        assertEquals([], $out);
    },

    'files-with-matches prints the label once when matched' => function () {
        $out = runFormat('a', ['a', 'a', 'a'], ['filesWithMatches' => true], showLabel: true, label: 'x.txt');
        assertEquals(['x.txt'], $out);
    },
];
