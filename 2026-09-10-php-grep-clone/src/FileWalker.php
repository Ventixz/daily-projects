<?php

declare(strict_types=1);

namespace PhGrep;

/**
 * Expands the CLI's raw path list into a flat list of readable files,
 * descending into directories only when -r/-R was given.
 */
final class FileWalker
{
    /**
     * @param string[] $paths
     * @return string[]
     */
    public static function expand(array $paths, bool $recursive): array
    {
        $files = [];
        foreach ($paths as $path) {
            if (is_dir($path)) {
                if (!$recursive) {
                    fwrite(STDERR, "grep.php: $path: Is a directory\n");
                    continue;
                }
                foreach (self::walkDir($path) as $file) {
                    $files[] = $file;
                }
                continue;
            }
            $files[] = $path;
        }
        return $files;
    }

    /**
     * @return \Generator<string>
     */
    private static function walkDir(string $dir): \Generator
    {
        $entries = scandir($dir);
        if ($entries === false) {
            return;
        }
        sort($entries);
        foreach ($entries as $entry) {
            if ($entry === '.' || $entry === '..') {
                continue;
            }
            $full = $dir . DIRECTORY_SEPARATOR . $entry;
            if (is_dir($full)) {
                yield from self::walkDir($full);
            } else {
                yield $full;
            }
        }
    }
}
