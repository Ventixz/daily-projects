<?php
declare(strict_types=1);

// Front controller. Run with:  php -S 127.0.0.1:8080 public/index.php
// then talk to http://<tenant>.localtest.me:8080 (localtest.me and all its
// subdomains resolve to 127.0.0.1 in public DNS).

require __DIR__ . '/../src/autoload.php';

use Tenancy\App;
use Tenancy\TenantManager;

$storage = getenv('TENANCY_STORAGE') ?: __DIR__ . '/../storage';
$domain = getenv('TENANCY_DOMAIN') ?: 'localtest.me';

session_name('tenancy_sid');
session_set_cookie_params([
    'lifetime' => 0,
    'path' => '/',
    // No 'domain': the cookie is host-only, so the browser sends acme's cookie
    // only to acme.localtest.me, never to globex.localtest.me.
    'httponly' => true,
    'samesite' => 'Lax',
    'secure' => !empty($_SERVER['HTTPS']),
]);
session_start();

$app = new App(new TenantManager($storage, $domain));
$session = $_SESSION;
[$status, $payload] = $app->handle(
    $_SERVER['REQUEST_METHOD'] ?? 'GET',
    $_SERVER['HTTP_HOST'] ?? '',
    $_SERVER['REQUEST_URI'] ?? '/',
    $_SERVER['CONTENT_TYPE'] ?? '',
    (string) file_get_contents('php://input'),
    $session,
);

if (!empty($session['regenerate'])) {
    // New session id on login, so an id planted before login (session fixation) is useless.
    unset($session['regenerate']);
    session_regenerate_id(true);
}
$_SESSION = $session;
if ($session === []) {
    session_destroy();
}

http_response_code($status);
header('Content-Type: application/json');
echo json_encode($payload, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES), "\n";
