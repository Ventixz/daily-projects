<?php

declare(strict_types=1);

namespace PhGrep;

final class UsageError extends \RuntimeException
{
}

/**
 * Parsed command-line options, immutable once built.
 */
final class Options
{
    public function __construct(
        public readonly string $pattern,
        public readonly array $paths,
        public readonly bool $ignoreCase = false,
        public readonly bool $invertMatch = false,
        public readonly bool $lineNumber = false,
        public readonly bool $countOnly = false,
        public readonly bool $filesWithMatches = false,
        public readonly bool $recursive = false,
        public readonly bool $wholeWord = false,
        public readonly bool $fixedString = false,
        public readonly int $before = 0,
        public readonly int $after = 0,
    ) {
    }

    /**
     * @param string[] $argv Everything after the script name (no $argv[0]).
     */
    public static function parse(array $argv): self
    {
        $ignoreCase = false;
        $invertMatch = false;
        $lineNumber = false;
        $countOnly = false;
        $filesWithMatches = false;
        $recursive = false;
        $wholeWord = false;
        $fixedString = false;
        $before = 0;
        $after = 0;

        $positional = [];
        $i = 0;
        $n = count($argv);
        $sawDoubleDash = false;

        while ($i < $n) {
            $arg = $argv[$i];

            if (!$sawDoubleDash && $arg === '--') {
                $sawDoubleDash = true;
                $i++;
                continue;
            }

            if (!$sawDoubleDash && $arg !== '-' && str_starts_with($arg, '-')) {
                // Long options first.
                if ($arg === '--help') {
                    throw new UsageError(self::usage());
                }
                if (str_starts_with($arg, '--context=')) {
                    $val = self::requireInt(substr($arg, strlen('--context=')), '--context');
                    $before = $after = $val;
                    $i++;
                    continue;
                }
                if (str_starts_with($arg, '--after-context=')) {
                    $after = self::requireInt(substr($arg, strlen('--after-context=')), '--after-context');
                    $i++;
                    continue;
                }
                if (str_starts_with($arg, '--before-context=')) {
                    $before = self::requireInt(substr($arg, strlen('--before-context=')), '--before-context');
                    $i++;
                    continue;
                }

                // Short options: bundled single-dash flags, e.g. "-inv", plus
                // "-A3" / "-A 3" style numeric arguments for context flags.
                $chars = str_split(substr($arg, 1));
                for ($c = 0; $c < count($chars); $c++) {
                    $flag = $chars[$c];
                    switch ($flag) {
                        case 'i':
                            $ignoreCase = true;
                            break;
                        case 'v':
                            $invertMatch = true;
                            break;
                        case 'n':
                            $lineNumber = true;
                            break;
                        case 'c':
                            $countOnly = true;
                            break;
                        case 'l':
                            $filesWithMatches = true;
                            break;
                        case 'r':
                        case 'R':
                            $recursive = true;
                            break;
                        case 'w':
                            $wholeWord = true;
                            break;
                        case 'F':
                            $fixedString = true;
                            break;
                        case 'E':
                            // Extended regex is PCRE's default mode here; accepted for
                            // familiarity with real grep but changes nothing.
                            break;
                        case 'A':
                        case 'B':
                        case 'C':
                            $rest = implode('', array_slice($chars, $c + 1));
                            if ($rest !== '') {
                                $value = self::requireInt($rest, "-$flag");
                            } else {
                                $i++;
                                if ($i >= $n) {
                                    throw new UsageError("option -$flag requires an argument");
                                }
                                $value = self::requireInt($argv[$i], "-$flag");
                            }
                            if ($flag === 'A') {
                                $after = $value;
                            } elseif ($flag === 'B') {
                                $before = $value;
                            } else {
                                $before = $after = $value;
                            }
                            $c = count($chars); // consumed the rest of this arg
                            break;
                        default:
                            throw new UsageError("unknown option -$flag");
                    }
                }
                $i++;
                continue;
            }

            $positional[] = $arg;
            $i++;
        }

        if (count($positional) < 1) {
            throw new UsageError(self::usage());
        }

        $pattern = array_shift($positional);
        $paths = $positional; // empty means "read stdin"

        return new self(
            pattern: $pattern,
            paths: $paths,
            ignoreCase: $ignoreCase,
            invertMatch: $invertMatch,
            lineNumber: $lineNumber,
            countOnly: $countOnly,
            filesWithMatches: $filesWithMatches,
            recursive: $recursive,
            wholeWord: $wholeWord,
            fixedString: $fixedString,
            before: $before,
            after: $after,
        );
    }

    private static function requireInt(string $raw, string $optionName): int
    {
        if (!preg_match('/^\d+$/', $raw)) {
            throw new UsageError("option $optionName requires a non-negative integer, got \"$raw\"");
        }
        return (int) $raw;
    }

    public static function usage(): string
    {
        return <<<TXT
        Usage: grep.php [OPTIONS] PATTERN [FILE...]

          -i            ignore case
          -v            invert match (print non-matching lines)
          -n            print line numbers
          -c            print only a count of matching lines per file
          -l            print only file names containing a match
          -r, -R        recurse into directories
          -w            match whole words only
          -F            treat PATTERN as a literal string, not a regex
          -E            extended regex (default; accepted for compatibility)
          -A NUM        print NUM lines of trailing context
          -B NUM        print NUM lines of leading context
          -C NUM        print NUM lines of context (both directions)
          --context=NUM, --after-context=NUM, --before-context=NUM  (long forms)

        With no FILE, reads standard input.
        TXT;
    }
}
