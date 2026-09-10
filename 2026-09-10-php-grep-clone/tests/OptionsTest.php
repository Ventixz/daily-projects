<?php

declare(strict_types=1);

require_once __DIR__ . '/../src/Options.php';

use PhGrep\Options;
use PhGrep\UsageError;

return [
    'pattern and files are positional' => function () {
        $o = Options::parse(['needle', 'a.txt', 'b.txt']);
        assertEquals('needle', $o->pattern);
        assertEquals(['a.txt', 'b.txt'], $o->paths);
    },

    'bundled short flags' => function () {
        $o = Options::parse(['-inv', 'needle', 'a.txt']);
        assertTrue($o->ignoreCase);
        assertTrue($o->invertMatch);
        assertTrue($o->lineNumber);
    },

    'separate short flags' => function () {
        $o = Options::parse(['-i', '-c', 'needle']);
        assertTrue($o->ignoreCase);
        assertTrue($o->countOnly);
    },

    '-A takes a glued numeric argument' => function () {
        $o = Options::parse(['-A3', 'needle']);
        assertEquals(3, $o->after);
        assertEquals(0, $o->before);
    },

    '-B takes a separate numeric argument' => function () {
        $o = Options::parse(['-B', '2', 'needle']);
        assertEquals(2, $o->before);
    },

    '-C sets both before and after' => function () {
        $o = Options::parse(['-C', '4', 'needle']);
        assertEquals(4, $o->before);
        assertEquals(4, $o->after);
    },

    'long form context flags' => function () {
        $o = Options::parse(['--after-context=2', '--before-context=1', 'needle']);
        assertEquals(2, $o->after);
        assertEquals(1, $o->before);
    },

    '-- ends option parsing' => function () {
        $o = Options::parse(['--', '-not-an-option', 'a.txt']);
        assertEquals('-not-an-option', $o->pattern);
        assertEquals(['a.txt'], $o->paths);
    },

    'no pattern is a usage error' => function () {
        try {
            Options::parse(['-i']);
            throw new TestFailure('expected UsageError');
        } catch (UsageError) {
            // expected
        }
    },

    'unknown flag is a usage error' => function () {
        try {
            Options::parse(['-Z', 'needle']);
            throw new TestFailure('expected UsageError');
        } catch (UsageError) {
            // expected
        }
    },

    'non-numeric context argument is a usage error' => function () {
        try {
            Options::parse(['-A', 'abc', 'needle']);
            throw new TestFailure('expected UsageError');
        } catch (UsageError) {
            // expected
        }
    },
];
