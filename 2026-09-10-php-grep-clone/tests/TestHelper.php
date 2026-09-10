<?php

declare(strict_types=1);

final class TestFailure extends Exception
{
}

function assertEquals(mixed $expected, mixed $actual, string $message = ''): void
{
    if ($expected !== $actual) {
        $expectedStr = var_export($expected, true);
        $actualStr = var_export($actual, true);
        $prefix = $message !== '' ? "{$message}\n" : '';
        throw new TestFailure("{$prefix}Expected: {$expectedStr}\nActual:   {$actualStr}");
    }
}

function assertTrue(bool $condition, string $message = ''): void
{
    if (!$condition) {
        throw new TestFailure($message !== '' ? $message : 'Expected condition to be true');
    }
}

function assertFalse(bool $condition, string $message = ''): void
{
    assertTrue(!$condition, $message !== '' ? $message : 'Expected condition to be false');
}

/**
 * Runs bin/grep.php as a real subprocess and captures stdout/exit code,
 * so the CLI-wiring tests exercise the exact same code path a user does.
 *
 * @param string[] $args
 * @return array{stdout: string, exitCode: int}
 */
function runGrep(array $args, ?string $stdin = null): array
{
    $script = __DIR__ . '/../bin/grep.php';
    $cmd = 'php ' . escapeshellarg($script);
    foreach ($args as $arg) {
        $cmd .= ' ' . escapeshellarg($arg);
    }

    $descriptors = [
        0 => ['pipe', 'r'],
        1 => ['pipe', 'w'],
        2 => ['pipe', 'w'],
    ];
    $process = proc_open($cmd, $descriptors, $pipes);
    if (!is_resource($process)) {
        throw new TestFailure("failed to launch: $cmd");
    }

    fwrite($pipes[0], $stdin ?? '');
    fclose($pipes[0]);
    $stdout = stream_get_contents($pipes[1]);
    fclose($pipes[1]);
    fclose($pipes[2]);
    $exitCode = proc_close($process);

    return ['stdout' => $stdout, 'exitCode' => $exitCode];
}
