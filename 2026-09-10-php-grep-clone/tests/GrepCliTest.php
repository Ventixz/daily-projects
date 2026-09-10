<?php

declare(strict_types=1);

function makeCliFixture(): string
{
    $root = sys_get_temp_dir() . '/phgrep_cli_' . bin2hex(random_bytes(4));
    mkdir($root . '/sub', 0777, true);
    file_put_contents($root . '/a.txt', "apple\nbanana\ncherry\n");
    file_put_contents($root . '/sub/b.txt', "banana split\ndate\n");
    return $root;
}

function removeCliFixture(string $root): void
{
    unlink($root . '/a.txt');
    unlink($root . '/sub/b.txt');
    rmdir($root . '/sub');
    rmdir($root);
}

return [
    'reads from stdin when no files are given' => function () {
        $result = runGrep(['-n', 'ba'], "apple\nbanana\n");
        assertEquals("2:banana\n", $result['stdout']);
        assertEquals(0, $result['exitCode']);
    },

    'exit code is 1 when nothing matches' => function () {
        $result = runGrep(['zzz'], "apple\nbanana\n");
        assertEquals('', $result['stdout']);
        assertEquals(1, $result['exitCode']);
    },

    'exit code is 2 and usage prints on bad invocation' => function () {
        $result = runGrep([]);
        assertEquals(2, $result['exitCode']);
    },

    'single file gets no filename prefix' => function () {
        $root = makeCliFixture();
        try {
            $result = runGrep(['banana', $root . '/a.txt']);
            assertEquals("banana\n", $result['stdout']);
        } finally {
            removeCliFixture($root);
        }
    },

    'recursive search prefixes every match with its filename' => function () {
        $root = makeCliFixture();
        try {
            $result = runGrep(['-r', 'banana', $root]);
            $lines = explode("\n", trim($result['stdout']));
            sort($lines);
            assertEquals([
                "$root/a.txt:banana",
                "$root/sub/b.txt:banana split",
            ], $lines);
            assertEquals(0, $result['exitCode']);
        } finally {
            removeCliFixture($root);
        }
    },

    'recursive -l lists only matching filenames' => function () {
        $root = makeCliFixture();
        try {
            $result = runGrep(['-rl', 'date', $root]);
            assertEquals("$root/sub/b.txt\n", $result['stdout']);
        } finally {
            removeCliFixture($root);
        }
    },
];
