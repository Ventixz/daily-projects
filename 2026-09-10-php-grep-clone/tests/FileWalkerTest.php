<?php

declare(strict_types=1);

require_once __DIR__ . '/../src/FileWalker.php';

use PhGrep\FileWalker;

function makeTempTree(): string
{
    $root = sys_get_temp_dir() . '/phgrep_walk_' . bin2hex(random_bytes(4));
    mkdir($root . '/sub', 0777, true);
    file_put_contents($root . '/a.txt', "a\n");
    file_put_contents($root . '/sub/b.txt', "b\n");
    return $root;
}

function removeTempTree(string $root): void
{
    foreach (glob($root . '/sub/*') as $f) {
        unlink($f);
    }
    rmdir($root . '/sub');
    foreach (glob($root . '/*') as $f) {
        unlink($f);
    }
    rmdir($root);
}

return [
    'non-recursive on a directory yields nothing and warns' => function () {
        $root = makeTempTree();
        try {
            $files = FileWalker::expand([$root], false);
            assertEquals([], $files);
        } finally {
            removeTempTree($root);
        }
    },

    'recursive descends into subdirectories' => function () {
        $root = makeTempTree();
        try {
            $files = FileWalker::expand([$root], true);
            sort($files);
            assertEquals([$root . '/a.txt', $root . '/sub/b.txt'], $files);
        } finally {
            removeTempTree($root);
        }
    },

    'a plain file path passes through unchanged' => function () {
        $root = makeTempTree();
        try {
            $files = FileWalker::expand([$root . '/a.txt'], false);
            assertEquals([$root . '/a.txt'], $files);
        } finally {
            removeTempTree($root);
        }
    },
];
