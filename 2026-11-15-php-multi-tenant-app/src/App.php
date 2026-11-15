<?php
declare(strict_types=1);

namespace Tenancy;

/**
 * The HTTP layer as a pure function: (method, host, path, body, session) ->
 * (status, JSON payload). public/index.php feeds it PHP's real request and
 * $_SESSION; the tests feed it plain arrays.
 */
final class App
{
    public function __construct(private readonly TenantManager $tenants)
    {
    }

    /**
     * @param array<string, mixed> $session mutated in place (login/logout)
     * @return array{0:int, 1:array<string, mixed>}
     */
    public function handle(string $method, string $host, string $path, string $contentType, string $rawBody, array &$session): array
    {
        try {
            $method = strtoupper($method);
            $path = '/' . trim((string) parse_url($path, PHP_URL_PATH), '/');
            $body = $this->parseBody($method, $contentType, $rawBody);

            $tenant = $this->tenants->resolveHost($host);
            if ($tenant === null) {
                return $this->central($method, $host, $path, $body);
            }
            return $this->tenantRoutes($tenant, $method, $path, $body, $session);
        } catch (ValidationError $e) {
            return [422, ['error' => $e->getMessage()]];
        } catch (Forbidden $e) {
            return [403, ['error' => $e->getMessage()]];
        } catch (HttpError $e) {
            return [$e->getCode(), ['error' => $e->getMessage()]];
        }
    }

    /** Routes on the bare domain (no tenant): landing page and sign-up. */
    private function central(string $method, string $host, string $path, array $body): array
    {
        $bare = strtolower(preg_replace('/:\d+$/', '', $host));
        if ($bare !== strtolower($this->tenants->baseDomain())) {
            // A subdomain nobody registered. Don't fall through to the sign-up site.
            throw new HttpError('no such tenant', 404);
        }
        if ($method === 'GET' && $path === '/') {
            return [200, ['app' => 'multi-tenant demo', 'signup' => 'POST /signup']];
        }
        if ($method === 'POST' && $path === '/signup') {
            $t = $this->tenants->create(
                $this->str($body, 'subdomain'),
                $this->str($body, 'organization'),
                $this->str($body, 'name'),
                $this->str($body, 'email'),
                $this->str($body, 'password'),
            );
            return [201, ['tenant' => $t->subdomain, 'host' => $t->subdomain . '.' . $this->tenants->baseDomain()]];
        }
        throw new HttpError('not found', 404);
    }

    private function tenantRoutes(Tenant $tenant, string $method, string $path, array $body, array &$session): array
    {
        $userId = $this->currentUser($tenant, $session);
        $route = $method . ' ' . $path;

        // Public routes on a tenant host.
        if ($route === 'GET /') {
            return [200, ['tenant' => $tenant->subdomain, 'name' => $tenant->name, 'logged_in' => $userId !== null]];
        }
        if ($route === 'POST /login') {
            $user = $tenant->auth()->attempt($this->str($body, 'email'), $this->str($body, 'password'));
            if ($user === null) {
                throw new HttpError('invalid credentials', 401);
            }
            $this->login($session, $tenant, $user['id']);
            return [200, ['user' => $user]];
        }
        if ($route === 'POST /logout') {
            $session = [];
            return [200, ['ok' => true]];
        }
        if (preg_match('#^/invitations/([0-9a-f]{64})$#', $path, $m) && $method === 'GET') {
            $inv = $tenant->invitations()->peek($m[1]);
            if ($inv === null) {
                throw new HttpError('invitation is invalid, expired or already used', 404);
            }
            return [200, $inv];
        }
        if (preg_match('#^/invitations/([0-9a-f]{64})/accept$#', $path, $m) && $method === 'POST') {
            $id = $tenant->invitations()->accept($m[1], $this->str($body, 'name'), $this->str($body, 'password'));
            $this->login($session, $tenant, $id);
            return [201, ['user' => $tenant->auth()->find($id)]];
        }

        // Everything below needs a logged-in user of *this* tenant.
        if ($userId === null) {
            throw new HttpError('login required', 401);
        }
        $rbac = $tenant->rbac();

        if ($route === 'GET /me') {
            return [200, [
                'user' => $tenant->auth()->find($userId),
                'roles' => $rbac->roles($userId),
                'permissions' => $rbac->permissions($userId),
            ]];
        }
        if ($route === 'GET /projects') {
            return [200, ['projects' => $tenant->projects()->all($userId)]];
        }
        if ($route === 'POST /projects') {
            return [201, ['id' => $tenant->projects()->create($userId, $this->str($body, 'title'))]];
        }
        if ($method === 'DELETE' && preg_match('#^/projects/(\d+)$#', $path, $m)) {
            if (!$tenant->projects()->delete($userId, (int) $m[1])) {
                throw new HttpError('not found', 404);
            }
            return [200, ['ok' => true]];
        }
        if ($route === 'GET /invitations') {
            $rbac->authorize($userId, 'invite users');
            return [200, ['pending' => $tenant->invitations()->pending()]];
        }
        if ($route === 'POST /invitations') {
            $token = $tenant->invitations()->invite($userId, $this->str($body, 'email'), $this->str($body, 'role'));
            // A real app emails this link. The demo returns it so you can follow it.
            return [201, ['accept_path' => "/invitations/{$token}"]];
        }
        if ($method === 'PUT' && preg_match('#^/users/(\d+)/role$#', $path, $m)) {
            $target = (int) $m[1];
            if ($tenant->auth()->find($target) === null) {
                throw new HttpError('not found', 404);
            }
            $rbac->setRole($userId, $target, $this->str($body, 'role'));
            return [200, ['roles' => $rbac->roles($target)]];
        }
        throw new HttpError('not found', 404);
    }

    /**
     * The session stores which tenant it belongs to. A session cookie from
     * tenant A replayed against tenant B's host is treated as logged out:
     * user id 1 at A is a different person from user id 1 at B.
     */
    private function currentUser(Tenant $tenant, array $session): ?int
    {
        if (($session['tenant_id'] ?? null) !== $tenant->id || !isset($session['user_id'])) {
            return null;
        }
        $id = (int) $session['user_id'];
        return $tenant->auth()->find($id) !== null ? $id : null;
    }

    private function login(array &$session, Tenant $tenant, int $userId): void
    {
        $session = ['tenant_id' => $tenant->id, 'user_id' => $userId, 'regenerate' => true];
    }

    private function parseBody(string $method, string $contentType, string $raw): array
    {
        if (!in_array($method, ['POST', 'PUT', 'PATCH', 'DELETE'], true)) {
            return [];
        }
        if ($raw === '') {
            return [];
        }
        // Requiring JSON is a cheap CSRF guard: a cross-site HTML form can't send
        // application/json, and a cross-site fetch() with it needs a CORS preflight.
        if (!str_starts_with(strtolower(trim($contentType)), 'application/json')) {
            throw new HttpError('request body must be application/json', 415);
        }
        $data = json_decode($raw, true);
        if (!is_array($data)) {
            throw new HttpError('malformed JSON body', 400);
        }
        return $data;
    }

    private function str(array $body, string $key): string
    {
        $v = $body[$key] ?? '';
        if (!is_string($v)) {
            throw new ValidationError("{$key} must be a string");
        }
        return $v;
    }
}
