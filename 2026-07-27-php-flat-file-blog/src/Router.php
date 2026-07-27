<?php

declare(strict_types=1);

final class Router
{
    /** @return array{route: string, params: array<string, string>} */
    public static function match(string $requestUri): array
    {
        $path = parse_url($requestUri, PHP_URL_PATH) ?? '/';
        $path = rtrim($path, '/');
        if ($path === '') {
            $path = '/';
        }

        if ($path === '/') {
            return ['route' => 'home', 'params' => []];
        }

        if (preg_match('#^/post/([a-z0-9\-]+)$#i', $path, $m)) {
            return ['route' => 'post', 'params' => ['slug' => $m[1]]];
        }

        return ['route' => 'not_found', 'params' => []];
    }
}
