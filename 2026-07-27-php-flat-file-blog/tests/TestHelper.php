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

function assertStringContains(string $needle, string $haystack, string $message = ''): void
{
    if (!str_contains($haystack, $needle)) {
        $prefix = $message !== '' ? "{$message}\n" : '';
        throw new TestFailure("{$prefix}Expected to find:\n{$needle}\nIn:\n{$haystack}");
    }
}
