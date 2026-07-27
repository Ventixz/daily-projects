<?php

declare(strict_types=1);

require __DIR__ . '/../src/Markdown.php';
require __DIR__ . '/../src/Post.php';
require __DIR__ . '/../src/Blog.php';
require __DIR__ . '/../src/Router.php';
require __DIR__ . '/TestHelper.php';

$testFiles = glob(__DIR__ . '/*Test.php');
sort($testFiles);

$total = 0;
$failures = [];

foreach ($testFiles as $file) {
    $suite = basename($file, '.php');
    $tests = require $file;

    foreach ($tests as $name => $test) {
        $total++;
        try {
            $test();
            echo "  ok  {$suite}: {$name}\n";
        } catch (Throwable $e) {
            $failures[] = "{$suite}: {$name}\n{$e->getMessage()}";
            echo "FAIL  {$suite}: {$name}\n";
        }
    }
}

$failureCount = count($failures);
echo "\n{$total} tests, {$failureCount} failures\n";

if ($failures !== []) {
    echo "\n" . implode("\n\n", $failures) . "\n";
    exit(1);
}

exit(0);
