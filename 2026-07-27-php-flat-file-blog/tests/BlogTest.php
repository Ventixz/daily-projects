<?php

declare(strict_types=1);

return [
    'loads posts sorted newest first and finds them by slug' => function (): void {
        $dir = sys_get_temp_dir() . '/blog_test_' . uniqid();
        mkdir($dir);
        file_put_contents("{$dir}/2024-01-01-old.md", "---\ntitle: Old\ndate: 2024-01-01\n---\nOld body.");
        file_put_contents("{$dir}/2024-06-01-new.md", "---\ntitle: New\ndate: 2024-06-01\n---\nNew body.");

        $blog = new Blog($dir);
        $posts = $blog->allPosts();

        assertEquals(2, count($posts));
        assertEquals('New', $posts[0]->title);
        assertEquals('Old', $posts[1]->title);
        assertEquals('New', $blog->findBySlug('2024-06-01-new')?->title);
        assertTrue($blog->findBySlug('does-not-exist') === null);

        array_map('unlink', glob("{$dir}/*.md"));
        rmdir($dir);
    },

    'an empty posts directory yields an empty list, not an error' => function (): void {
        $dir = sys_get_temp_dir() . '/blog_test_' . uniqid();
        mkdir($dir);

        $blog = new Blog($dir);

        assertEquals([], $blog->allPosts());

        rmdir($dir);
    },
];
