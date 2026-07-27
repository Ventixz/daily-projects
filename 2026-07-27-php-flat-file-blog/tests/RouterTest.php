<?php

declare(strict_types=1);

return [
    'matches the home route' => function (): void {
        $match = Router::match('/');
        assertEquals('home', $match['route']);
    },

    'matches a post route and captures its slug' => function (): void {
        $match = Router::match('/post/2024-01-01-hello-world?utm=ref');
        assertEquals('post', $match['route']);
        assertEquals('2024-01-01-hello-world', $match['params']['slug']);
    },

    'a trailing slash on the home route still matches home' => function (): void {
        $match = Router::match('/');
        assertEquals('home', $match['route']);
        assertEquals([], $match['params']);
    },

    'unknown paths fall through to not_found' => function (): void {
        $match = Router::match('/nope');
        assertEquals('not_found', $match['route']);
    },
];
