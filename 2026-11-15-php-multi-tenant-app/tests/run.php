<?php
declare(strict_types=1);

// Test runner with no framework: php tests/run.php

require __DIR__ . '/../src/autoload.php';

use Tenancy\App;
use Tenancy\Forbidden;
use Tenancy\Invitations;
use Tenancy\Migrator;
use Tenancy\TenantManager;
use Tenancy\ValidationError;

$checks = 0;
$failures = [];
$current = '';

function check(bool $cond, string $what): void
{
    global $checks, $failures, $current;
    $checks++;
    if (!$cond) {
        $failures[] = "[{$current}] {$what}";
    }
}

function throws(callable $fn, string $class, string $what, ?string $msgPart = null): void
{
    try {
        $fn();
        check(false, "{$what} (no exception)");
    } catch (\Throwable $e) {
        $ok = $e instanceof $class && ($msgPart === null || str_contains($e->getMessage(), $msgPart));
        check($ok, "{$what} (got " . get_class($e) . ': ' . $e->getMessage() . ')');
    }
}

function freshStorage(): string
{
    $dir = sys_get_temp_dir() . '/tenancy-test-' . bin2hex(random_bytes(4));
    mkdir($dir);
    register_shutdown_function(static function () use ($dir) {
        exec('rm -rf ' . escapeshellarg($dir));
    });
    return $dir;
}

function setup(): array
{
    $tm = new TenantManager(freshStorage(), 'example.test');
    $acme = $tm->create('acme', 'Acme Corp', 'Ada Admin', 'ada@acme.test', 'acme-pass-1');
    $globex = $tm->create('globex', 'Globex', 'Gil Admin', 'gil@globex.test', 'globex-pass-1');
    return [$tm, $acme, $globex];
}

$tests = [];

$tests['migrations run once and are recorded'] = function () {
    $pdo = Tenancy\Db::open(':memory:');
    check(Migrator::run($pdo, Migrator::tenant()) === 4, 'first run applies all 4');
    check(Migrator::run($pdo, Migrator::tenant()) === 0, 'second run applies none');
    $extra = Migrator::tenant() + ['0005_add_column' => 'ALTER TABLE projects ADD COLUMN archived INTEGER NOT NULL DEFAULT 0'];
    check(Migrator::run($pdo, $extra) === 1, 'a new migration is applied on its own');
    $bad = $extra + ['0006_broken' => 'CREATE TABLE ok_part (x INT); THIS IS NOT SQL'];
    throws(fn () => Migrator::run($pdo, $bad), \PDOException::class, 'broken migration throws');
    $names = $pdo->query('SELECT name FROM migrations')->fetchAll(PDO::FETCH_COLUMN);
    check(!in_array('0006_broken', $names, true), 'failed migration is not recorded');
    $t = $pdo->query("SELECT name FROM sqlite_master WHERE name = 'ok_part'")->fetchColumn();
    check($t === false, 'failed migration is rolled back as a unit');
};

$tests['subdomain validation'] = function () {
    [$tm] = setup();
    foreach (['ab', 'Has Space', '-lead', 'trail-', 'dou--ble', 'under_score', 'a.b', str_repeat('x', 31), '../etc'] as $bad) {
        throws(fn () => $tm->create($bad, 'X', 'X', 'x@x.test', 'password1'), ValidationError::class, "rejects '{$bad}'");
    }
    throws(fn () => $tm->create('www', 'X', 'X', 'x@x.test', 'password1'), ValidationError::class, 'reserved', 'reserved');
    throws(fn () => $tm->create('ACME', 'X', 'X', 'x@x.test', 'password1'), ValidationError::class, 'taken (case-insensitive)', 'taken');
    $t = $tm->create('  New-Co ', 'New Co', 'N', 'n@new.test', 'password1');
    check($t->subdomain === 'new-co', 'subdomain is trimmed and lower-cased');
};

$tests['each tenant gets its own database file with a random name'] = function () {
    [$tm, $acme, $globex] = setup();
    check($acme->dbPath !== $globex->dbPath, 'different files');
    check(is_file($acme->dbPath) && is_file($globex->dbPath), 'files exist');
    check(!str_contains(basename($acme->dbPath), 'acme'), 'file name not derived from subdomain');
    check(count($tm->all()) === 2, 'two tenants listed');
};

$tests['failed sign-up leaves no orphan database'] = function () {
    $dir = freshStorage();
    $tm = new TenantManager($dir, 'example.test');
    // Empty admin name fails *after* the database file has been created and migrated.
    throws(fn () => $tm->create('orphan', 'O', '   ', 'o@o.test', 'password1'), ValidationError::class, 'blank name rejected');
    check(glob($dir . '/tenants/*.sqlite') === [], 'no database file left behind');
    check($tm->findBySubdomain('orphan') === null, 'tenant not registered');
};

$tests['host resolution'] = function () {
    [$tm] = setup();
    check($tm->resolveHost('acme.example.test')?->subdomain === 'acme', 'plain host');
    check($tm->resolveHost('ACME.Example.Test:8080')?->subdomain === 'acme', 'case and port ignored');
    check($tm->resolveHost('example.test') === null, 'bare domain is not a tenant');
    check($tm->resolveHost('nope.example.test') === null, 'unknown subdomain');
    check($tm->resolveHost('x.acme.example.test') === null, 'nested subdomain is not acme');
    check($tm->resolveHost('acme.example.test.evil.com') === null, 'suffix must match');
    check($tm->resolveHost('acmeexample.test') === null, 'no dot before base domain');
};

$tests['tenant data is isolated'] = function () {
    [$tm, $acme, $globex] = setup();
    $ada = $acme->auth()->attempt('ada@acme.test', 'acme-pass-1')['id'];
    $gil = $globex->auth()->attempt('gil@globex.test', 'globex-pass-1')['id'];
    check($ada === 1 && $gil === 1, 'both admins are user id 1 in their own tenant');
    $acme->projects()->create($ada, 'Rocket skates');
    check(count($acme->projects()->all($ada)) === 1, 'acme sees its project');
    check($globex->projects()->all($gil) === [], 'globex sees nothing of acme');
    check($globex->auth()->attempt('ada@acme.test', 'acme-pass-1') === null, "acme's user can't log in at globex");

    // The same email can be a separate account in each tenant.
    $tokenG = $globex->invitations()->invite($gil, 'ada@acme.test', 'viewer');
    $globex->invitations()->accept($tokenG, 'Ada at Globex', 'other-pass-2');
    check($globex->auth()->attempt('ada@acme.test', 'other-pass-2') !== null, 'globex account works');
    check($acme->auth()->attempt('ada@acme.test', 'other-pass-2') === null, "globex password doesn't open acme account");
    check($acme->rbac()->roles($ada) === ['admin'] && $globex->rbac()->roles(2) === ['viewer'], 'roles are per tenant');
};

$tests['default roles and permissions'] = function () {
    [, $acme] = setup();
    $rbac = $acme->rbac();
    check($rbac->roles(1) === ['admin'], 'sign-up user is admin');
    check($rbac->can(1, 'delete projects') && $rbac->can(1, 'invite users'), 'admin can do admin things');
    $inv = $acme->invitations();
    $mem = $inv->accept($inv->invite(1, 'm@acme.test', 'member'), 'Mem', 'member-pass');
    $view = $inv->accept($inv->invite(1, 'v@acme.test', 'viewer'), 'View', 'viewer-pass');
    check($rbac->can($mem, 'create projects') && !$rbac->can($mem, 'delete projects'), 'member creates, not deletes');
    check($rbac->can($view, 'view projects') && !$rbac->can($view, 'create projects'), 'viewer only views');
    $p = $acme->projects()->create($mem, 'Member project');
    throws(fn () => $acme->projects()->delete($mem, $p), Forbidden::class, 'member cannot delete');
    throws(fn () => $acme->projects()->create($view, 'x'), Forbidden::class, 'viewer cannot create');
    throws(fn () => $inv->invite($mem, 'z@acme.test', 'admin'), Forbidden::class, 'member cannot invite (or self-escalate)');
    check($acme->projects()->delete(1, $p), 'admin can delete');
    check($rbac->can(999, 'view projects') === false, 'unknown user has no permissions');
};

$tests['role changes'] = function () {
    [, $acme] = setup();
    $rbac = $acme->rbac();
    $inv = $acme->invitations();
    $mem = $inv->accept($inv->invite(1, 'm@acme.test', 'member'), 'Mem', 'member-pass');
    check(!$rbac->can($mem, 'invite users'), 'member cannot invite yet');
    throws(fn () => $rbac->setRole($mem, $mem, 'admin'), Forbidden::class, 'member cannot promote self');
    $rbac->setRole(1, $mem, 'admin');
    check($rbac->can($mem, 'invite users'), 'permission cache invalidated after promotion');
    check($rbac->roles($mem) === ['admin'], 'setRole replaces old roles');
    throws(fn () => $rbac->setRole(1, $mem, 'wizard'), ValidationError::class, 'unknown role rejected');
    check($rbac->roles($mem) === ['admin'], 'roles unchanged after rejected change');

    $rbac->setRole($mem, 1, 'member');                      // 2 admins -> 1: allowed
    throws(fn () => $rbac->setRole($mem, $mem, 'member'), ValidationError::class, 'last admin protected', 'last admin');
    check($rbac->roles($mem) === ['admin'], 'last admin keeps the role (transaction rolled back)');
};

$tests['invitations'] = function () {
    [, $acme] = setup();
    $now = 1_700_000_000;
    $inv = new Invitations($acme->db(), $acme->rbac(), function () use (&$now) { return $now; });

    $token = $inv->invite(1, ' New@Acme.test ', 'member');
    check(strlen($token) === 64 && ctype_xdigit($token), '256-bit hex token');
    $stored = $acme->db()->query('SELECT email, token_hash FROM invitations')->fetch();
    check($stored['token_hash'] === hash('sha256', $token) && $stored['token_hash'] !== $token, 'only the hash is stored');
    check($stored['email'] === 'new@acme.test', 'email normalised');
    check($inv->peek($token) === ['email' => 'new@acme.test', 'role' => 'member'], 'peek shows email and role');
    check($inv->peek(str_repeat('0', 64)) === null && $inv->peek('short') === null, 'bad tokens are unknown');
    check(count($inv->pending()) === 1, 'listed as pending');

    $newer = $inv->invite(1, 'new@acme.test', 'viewer');
    check($inv->peek($token) === null, 're-invite kills the old link');
    check($inv->peek($newer)['role'] === 'viewer', 'new link has new role');

    $now += Invitations::TTL_SECONDS + 1;
    throws(fn () => $inv->accept($newer, 'Late', 'late-pass-1'), ValidationError::class, 'expired link rejected');
    check($inv->pending() === [], 'expired invitations not pending');

    $fresh = $inv->invite(1, 'new@acme.test', 'member');
    throws(fn () => $inv->accept($fresh, 'Short', 'short'), ValidationError::class, 'weak password rejected');
    check($inv->peek($fresh) !== null, 'failed accept leaves the link usable (rolled back)');
    $uid = $inv->accept($fresh, 'Newbie', 'newbie-pass');
    check($acme->rbac()->roles($uid) === ['member'], 'accepted user has invited role');
    throws(fn () => $inv->accept($fresh, 'Again', 'again-pass-1'), ValidationError::class, 'link is single-use');
    throws(fn () => $inv->invite(1, 'NEW@acme.test', 'viewer'), ValidationError::class, 'cannot invite existing member', 'already');
    throws(fn () => $inv->invite(1, 'not-an-email', 'viewer'), ValidationError::class, 'invalid email');
    throws(fn () => $inv->invite(1, 'z@acme.test', 'overlord'), ValidationError::class, 'unknown role');
};

$tests['auth'] = function () {
    [, $acme] = setup();
    $auth = $acme->auth();
    check($auth->attempt('ADA@acme.test', 'acme-pass-1')['id'] === 1, 'email is case-insensitive');
    check($auth->attempt('ada@acme.test', 'wrong-pass') === null, 'wrong password');
    check($auth->attempt('ghost@acme.test', 'acme-pass-1') === null, 'unknown user');
    $hash = $acme->db()->query('SELECT password_hash FROM users WHERE id = 1')->fetchColumn();
    check(password_get_info($hash)['algo'] !== null && !str_contains($hash, 'acme-pass-1'), 'password is hashed');
    throws(fn () => $auth->register('Dup', 'ada@acme.test', 'password1'), ValidationError::class, 'duplicate email', 'already');
};

$tests['migrateAll upgrades every tenant'] = function () {
    [$tm, $acme, $globex] = setup();
    check($tm->migrateAll() === 0, 'nothing pending after sign-up');
    check($tm->delete('globex') && !is_file($globex->dbPath), 'delete removes the database file');
    check($tm->findBySubdomain('globex') === null && !$tm->delete('globex'), 'tenant gone');
};

// ---- HTTP layer, called directly ----

function req(App $app, string $method, string $host, string $path, ?array $body, array &$session, string $ct = 'application/json'): array
{
    return $app->handle($method, $host, $path, $ct, $body === null ? '' : json_encode($body), $session);
}

$tests['app: sign-up, login, permissions'] = function () {
    $tm = new TenantManager(freshStorage(), 'example.test');
    $app = new App($tm);
    $s = [];
    [$st, $p] = req($app, 'POST', 'example.test', '/signup', ['subdomain' => 'initech', 'organization' => 'Initech',
        'name' => 'Bill', 'email' => 'bill@initech.test', 'password' => 'tps-reports'], $s);
    check($st === 201 && $p['host'] === 'initech.example.test', 'sign-up on bare domain');
    [$st] = req($app, 'POST', 'example.test', '/signup', ['subdomain' => 'initech', 'organization' => 'X',
        'name' => 'X', 'email' => 'x@x.test', 'password' => 'password1'], $s);
    check($st === 422, 'duplicate sign-up is 422');

    $h = 'initech.example.test:8080';
    check(req($app, 'GET', $h, '/projects', null, $s)[0] === 401, 'login required');
    check(req($app, 'POST', $h, '/login', ['email' => 'bill@initech.test', 'password' => 'nope-nope'], $s)[0] === 401, 'bad login is 401');
    [$st, $p] = req($app, 'POST', $h, '/login', ['email' => 'bill@initech.test', 'password' => 'tps-reports'], $s);
    check($st === 200 && $s['user_id'] === 1 && !empty($s['regenerate']), 'login sets session and asks for a new id');
    check(req($app, 'POST', $h, '/projects', ['title' => 'TPS'], $s)[0] === 201, 'create project');

    [$st, $p] = req($app, 'POST', $h, '/invitations', ['email' => 'peter@initech.test', 'role' => 'viewer'], $s);
    check($st === 201 && str_starts_with($p['accept_path'], '/invitations/'), 'invite returns link');
    $link = $p['accept_path'];
    check(req($app, 'GET', $h, '/invitations', null, $s)[1]['pending'][0]['email'] === 'peter@initech.test', 'pending list');

    $peter = [];
    check(req($app, 'GET', $h, $link, null, $peter)[1]['role'] === 'viewer', 'invitee can view link without login');
    [$st] = req($app, 'POST', $h, $link . '/accept', ['name' => 'Peter', 'password' => 'no-flair-1'], $peter);
    check($st === 201 && $peter['user_id'] === 2, 'accept logs the invitee in');
    check(req($app, 'GET', $h, '/projects', null, $peter)[0] === 200, 'viewer can list');
    [$st, $p] = req($app, 'POST', $h, '/projects', ['title' => 'Nope'], $peter);
    check($st === 403 && str_contains($p['error'], 'create projects'), 'viewer cannot create: 403');
    check(req($app, 'PUT', $h, '/users/2/role', ['role' => 'member'], $peter)[0] === 403, 'viewer cannot change roles');
    check(req($app, 'PUT', $h, '/users/2/role', ['role' => 'member'], $s)[1]['roles'] === ['member'], 'admin promotes');
    check(req($app, 'POST', $h, '/projects', ['title' => 'Now OK'], $peter)[0] === 201, 'promotion takes effect at once');
    check(req($app, 'GET', $h, '/me', null, $peter)[1]['roles'] === ['member'], '/me shows roles');
    check(req($app, 'PUT', $h, '/users/99/role', ['role' => 'member'], $s)[0] === 404, 'unknown user 404');
    check(req($app, 'DELETE', $h, '/projects/1', null, $s)[0] === 200, 'admin deletes');
    check(req($app, 'DELETE', $h, '/projects/1', null, $s)[0] === 404, 'second delete 404');
    check(req($app, 'POST', $h, '/logout', null, $peter)[0] === 200 && $peter === [], 'logout clears session');
};

$tests['app: tenant boundary and request hygiene'] = function () {
    [$tm] = setup();
    $app = new App($tm);
    $s = [];
    req($app, 'POST', 'acme.example.test', '/login', ['email' => 'ada@acme.test', 'password' => 'acme-pass-1'], $s);
    check(req($app, 'GET', 'acme.example.test', '/me', null, $s)[0] === 200, 'session works on its own tenant');
    // Replay acme's session against globex: both have a user id 1, but it's not the same person.
    [$st] = req($app, 'GET', 'globex.example.test', '/me', null, $s);
    check($st === 401, "acme's session is not valid at globex");
    check(req($app, 'GET', 'globex.example.test', '/projects', null, $s)[0] === 401, 'nor for its data');

    check(req($app, 'GET', 'ghost.example.test', '/', null, $s)[0] === 404, 'unknown tenant 404');
    check(req($app, 'POST', 'ghost.example.test', '/signup', ['x' => 1], $s)[0] === 404, 'sign-up not served on unknown subdomain');
    check(req($app, 'GET', 'other.domain', '/', null, $s)[0] === 404, 'foreign host 404');
    [$st] = $app->handle('POST', 'acme.example.test', '/projects', 'application/x-www-form-urlencoded', 'title=csrf', $s);
    check($st === 415, 'form-encoded body refused (CSRF guard)');
    [$st] = $app->handle('POST', 'acme.example.test', '/projects', 'application/json', '{not json', $s);
    check($st === 400, 'malformed JSON 400');
    [$st] = req($app, 'POST', 'acme.example.test', '/projects', ['title' => ['array']], $s);
    check($st === 422, 'non-string field 422');
    check(req($app, 'GET', 'acme.example.test', '/nope', null, $s)[0] === 404, 'unknown route 404');

    // A deleted user's session stops working.
    $acme = $tm->findBySubdomain('acme');
    $inv = $acme->invitations();
    $uid = $inv->accept($inv->invite(1, 'tmp@acme.test', 'viewer'), 'Tmp', 'tmp-pass-1');
    $ts = ['tenant_id' => $acme->id, 'user_id' => $uid];
    $acme->db()->prepare('DELETE FROM users WHERE id = ?')->execute([$uid]);
    check(req($app, 'GET', 'acme.example.test', '/me', null, $ts)[0] === 401, 'deleted user is logged out');
};

// ---- Real HTTP through php -S, with cookies ----

$tests['integration: php -S with Host headers and cookies'] = function () {
    $storage = freshStorage();
    $port = 18000 + random_int(0, 999);
    $root = dirname(__DIR__);
    $cmd = sprintf('TENANCY_STORAGE=%s TENANCY_DOMAIN=example.test exec php -S 127.0.0.1:%d %s/public/index.php',
        escapeshellarg($storage), $port, escapeshellarg($root));
    $proc = proc_open($cmd, [1 => ['file', '/dev/null', 'w'], 2 => ['file', '/dev/null', 'w']], $pipes);
    register_shutdown_function(static fn () => proc_terminate($proc));
    for ($i = 0; $i < 50 && !@fsockopen('127.0.0.1', $port); $i++) {
        usleep(100_000);
    }

    $http = function (string $method, string $host, string $path, ?array $body = null, ?string $cookie = null) use ($port): array {
        $headers = "Host: {$host}\r\nContent-Type: application/json\r\n" . ($cookie ? "Cookie: {$cookie}\r\n" : '');
        $ctx = stream_context_create(['http' => ['method' => $method, 'header' => $headers,
            'content' => $body === null ? '' : json_encode($body), 'ignore_errors' => true]]);
        $resp = file_get_contents("http://127.0.0.1:{$port}{$path}", false, $ctx);
        $status = (int) explode(' ', $http_response_header[0])[1];
        $setCookie = null;
        foreach ($http_response_header as $h) {
            if (preg_match('/^Set-Cookie: (tenancy_sid=[^;]+)(.*)$/i', $h, $m)) {
                $setCookie = $m[1];
                check(stripos($m[2], 'HttpOnly') !== false && stripos($m[2], 'SameSite=Lax') !== false, 'cookie flags');
                check(stripos($m[2], 'domain=') === false, 'cookie is host-only');
            }
        }
        return [$status, json_decode((string) $resp, true), $setCookie];
    };

    [$st] = $http('POST', 'example.test', '/signup', ['subdomain' => 'acme', 'organization' => 'Acme',
        'name' => 'Ada', 'email' => 'ada@acme.test', 'password' => 'acme-pass-1']);
    check($st === 201, 'sign-up over HTTP');
    $http('POST', 'example.test', '/signup', ['subdomain' => 'globex', 'organization' => 'Globex',
        'name' => 'Gil', 'email' => 'gil@globex.test', 'password' => 'globex-pass-1']);

    [$st, , $cookie] = $http('POST', 'acme.example.test:' . $port, '/login', ['email' => 'ada@acme.test', 'password' => 'acme-pass-1']);
    check($st === 200 && $cookie !== null, 'login sets a cookie');
    [$st, $p] = $http('GET', 'acme.example.test', '/me', null, $cookie);
    check($st === 200 && $p['user']['email'] === 'ada@acme.test', 'cookie authenticates at acme');
    [$st] = $http('GET', 'globex.example.test', '/me', null, $cookie);
    check($st === 401, 'same cookie replayed at globex is rejected');
    [$st] = $http('GET', 'nobody.example.test', '/', null);
    check($st === 404, 'unknown tenant over HTTP');
    [$st] = $http('POST', 'acme.example.test', '/logout', null, $cookie);
    [$st2] = $http('GET', 'acme.example.test', '/me', null, $cookie);
    check($st === 200 && $st2 === 401, 'logout invalidates the server-side session');
};

// ---- run ----
$ran = 0;
foreach ($tests as $name => $fn) {
    $current = $name;
    $before = count($failures);
    try {
        $fn();
    } catch (\Throwable $e) {
        $failures[] = "[{$name}] uncaught " . get_class($e) . ': ' . $e->getMessage() . ' @ ' . $e->getFile() . ':' . $e->getLine();
    }
    $ran++;
    echo (count($failures) === $before ? 'ok   ' : 'FAIL ') . $name . "\n";
}
echo "\n{$ran} tests, {$checks} checks, " . count($failures) . " failures\n";
foreach ($failures as $f) {
    echo "  - {$f}\n";
}
exit($failures ? 1 : 0);
