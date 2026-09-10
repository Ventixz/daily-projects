<?php

declare(strict_types=1);

require_once __DIR__ . '/../src/Options.php';
require_once __DIR__ . '/../src/PatternMatcher.php';

use PhGrep\PatternMatcher;
use PhGrep\UsageError;

return [
    'plain regex matches' => function () {
        $m = new PatternMatcher('ba+r', false, false, false);
        assertTrue($m->matches('a baaar b'));
        assertFalse($m->matches('a bor b'));
    },

    'ignore case' => function () {
        $m = new PatternMatcher('HELLO', true, false, false);
        assertTrue($m->matches('well hello there'));
    },

    'case sensitive by default' => function () {
        $m = new PatternMatcher('HELLO', false, false, false);
        assertFalse($m->matches('well hello there'));
    },

    'whole word only matches word boundaries' => function () {
        $m = new PatternMatcher('cat', false, true, false);
        assertTrue($m->matches('the cat sat'));
        assertFalse($m->matches('concatenate'));
    },

    'fixed string treats metacharacters literally' => function () {
        $m = new PatternMatcher('$5.00', false, false, true);
        assertTrue($m->matches('price: $5.00 today'));
        assertFalse($m->matches('price: X5X00 today'), 'preg metachars must not act as regex when -F is set');
    },

    'invalid regex is rejected up front' => function () {
        try {
            new PatternMatcher('(unclosed', false, false, false);
            throw new TestFailure('expected UsageError for invalid regex');
        } catch (UsageError) {
            // expected
        }
    },
];
