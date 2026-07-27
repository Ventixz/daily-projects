<?php

declare(strict_types=1);

require __DIR__ . '/src/Blog.php';
require __DIR__ . '/src/Router.php';

// Let PHP's built-in server hand back static assets (style.css) unchanged,
// so this router script only runs for actual page requests.
if (PHP_SAPI === 'cli-server') {
    $path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH) ?? '/';
    $file = __DIR__ . $path;
    if ($path !== '/' && is_file($file)) {
        return false;
    }
}

function render(string $template, array $vars = []): void
{
    extract($vars);
    require __DIR__ . "/templates/{$template}.php";
}

$blog = new Blog(__DIR__ . '/posts');
$match = Router::match($_SERVER['REQUEST_URI'] ?? '/');

switch ($match['route']) {
    case 'home':
        render('home', ['posts' => $blog->allPosts()]);
        break;

    case 'post':
        $post = $blog->findBySlug($match['params']['slug']);
        if ($post === null) {
            http_response_code(404);
            render('not_found');
            break;
        }
        render('post', ['post' => $post]);
        break;

    default:
        http_response_code(404);
        render('not_found');
}
