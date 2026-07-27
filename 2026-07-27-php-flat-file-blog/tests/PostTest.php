<?php

declare(strict_types=1);

return [
    'parses front matter and renders the body as markdown' => function (): void {
        $tmp = sys_get_temp_dir() . '/post_test_' . uniqid() . '.md';
        file_put_contents($tmp, "---\ntitle: Test Post\ndate: 2024-05-01\n---\nHello **world**.");

        $post = Post::fromFile($tmp);

        assertEquals('Test Post', $post->title);
        assertEquals('2024-05-01', $post->date->format('Y-m-d'));
        assertStringContains('<strong>world</strong>', $post->renderedBody());

        unlink($tmp);
    },

    'falls back to the filename as slug and title when front matter is missing' => function (): void {
        $tmp = sys_get_temp_dir() . '/post_test_' . uniqid() . '.md';
        file_put_contents($tmp, 'Just a body, no front matter.');

        $post = Post::fromFile($tmp);

        assertEquals(pathinfo($tmp, PATHINFO_FILENAME), $post->title);
        assertEquals(pathinfo($tmp, PATHINFO_FILENAME), $post->slug);

        unlink($tmp);
    },
];
