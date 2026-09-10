<?php

declare(strict_types=1);

namespace PhGrep;

/**
 * Turns a grep-style pattern + flags into one compiled PCRE and a
 * yes/no test per line. Keeping this as its own class means Grep never
 * has to know whether the user asked for -F, -i, or -w — by the time it
 * has a PatternMatcher, that's already baked in.
 */
final class PatternMatcher
{
    private string $compiled;

    public function __construct(string $pattern, bool $ignoreCase, bool $wholeWord, bool $fixedString)
    {
        $body = $fixedString ? preg_quote($pattern, '/') : $pattern;

        if ($wholeWord) {
            $body = '\b(?:' . $body . ')\b';
        }

        $flags = $ignoreCase ? 'i' : '';
        $delimited = '/' . $body . '/' . $flags;

        if (@preg_match($delimited, '') === false) {
            throw new UsageError("invalid pattern: $pattern");
        }

        $this->compiled = $delimited;
    }

    public function matches(string $line): bool
    {
        return preg_match($this->compiled, $line) === 1;
    }
}
