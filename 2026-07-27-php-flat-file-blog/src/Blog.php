<?php

declare(strict_types=1);

require_once __DIR__ . '/Post.php';

final class Blog
{
    /** @var list<Post> */
    private array $posts;

    public function __construct(private readonly string $postsDir)
    {
        $this->posts = $this->loadPosts();
    }

    /** @return list<Post> */
    public function allPosts(): array
    {
        return $this->posts;
    }

    public function findBySlug(string $slug): ?Post
    {
        foreach ($this->posts as $post) {
            if ($post->slug === $slug) {
                return $post;
            }
        }

        return null;
    }

    /** @return list<Post> */
    private function loadPosts(): array
    {
        $files = glob(rtrim($this->postsDir, '/') . '/*.md') ?: [];
        $posts = array_map(static fn (string $file) => Post::fromFile($file), $files);

        usort($posts, static fn (Post $a, Post $b) => $b->date <=> $a->date);

        return $posts;
    }
}
