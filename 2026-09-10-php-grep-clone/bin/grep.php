#!/usr/bin/env php
<?php

declare(strict_types=1);

require __DIR__ . '/../src/Options.php';
require __DIR__ . '/../src/PatternMatcher.php';
require __DIR__ . '/../src/FileWalker.php';
require __DIR__ . '/../src/SearchResult.php';
require __DIR__ . '/../src/Grep.php';
require __DIR__ . '/../src/Formatter.php';

use PhGrep\FileWalker;
use PhGrep\Formatter;
use PhGrep\Grep;
use PhGrep\Options;
use PhGrep\PatternMatcher;
use PhGrep\UsageError;

/**
 * @return string[]
 */
function readLines(string $contents): array
{
    if ($contents === '') {
        return [];
    }
    $normalized = rtrim($contents, "\n");
    return $normalized === '' ? [''] : explode("\n", $normalized);
}

function main(array $argv): int
{
    try {
        $options = Options::parse(array_slice($argv, 1));
    } catch (UsageError $e) {
        fwrite(STDERR, $e->getMessage() . "\n");
        return 2;
    }

    try {
        $matcher = new PatternMatcher(
            $options->pattern,
            $options->ignoreCase,
            $options->wholeWord,
            $options->fixedString,
        );
    } catch (UsageError $e) {
        fwrite(STDERR, "grep.php: {$e->getMessage()}\n");
        return 2;
    }

    $grep = new Grep($matcher, $options);
    $formatter = new Formatter($options);

    if ($options->paths === []) {
        $lines = readLines((string) stream_get_contents(STDIN));
        $result = $grep->search($lines);
        foreach ($formatter->format('(standard input)', false, $result) as $line) {
            echo $line, "\n";
        }
        return $result->matchCount() > 0 ? 0 : 1;
    }

    $files = FileWalker::expand($options->paths, $options->recursive);
    $showLabel = count($files) > 1;
    $anyMatch = false;
    $hadError = false;

    foreach ($files as $file) {
        $contents = @file_get_contents($file);
        if ($contents === false) {
            fwrite(STDERR, "grep.php: $file: No such file or could not be read\n");
            $hadError = true;
            continue;
        }
        $result = $grep->search(readLines($contents));
        if ($result->matchCount() > 0) {
            $anyMatch = true;
        }
        foreach ($formatter->format($file, $showLabel, $result) as $line) {
            echo $line, "\n";
        }
    }

    if ($hadError) {
        return 2;
    }
    return $anyMatch ? 0 : 1;
}

exit(main($argv));
