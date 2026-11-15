<?php
declare(strict_types=1);

namespace Tenancy;

use PDO;

/**
 * Creates, finds and deletes tenants. The system database maps a subdomain to a
 * database file; every tenant's users, roles and data live only in that file.
 */
final class TenantManager
{
    /** Subdomains that would collide with the app's own hosts. */
    private const RESERVED = ['www', 'app', 'api', 'admin', 'mail'];

    private PDO $system;

    public function __construct(private readonly string $storageDir, private readonly string $baseDomain)
    {
        if (!is_dir($storageDir . '/tenants') && !mkdir($storageDir . '/tenants', 0775, true)) {
            throw new \RuntimeException("cannot create {$storageDir}/tenants");
        }
        $this->system = Db::open($storageDir . '/system.sqlite');
        Migrator::run($this->system, Migrator::system());
    }

    public function baseDomain(): string
    {
        return $this->baseDomain;
    }

    public static function validateSubdomain(string $subdomain): void
    {
        // A DNS label: lowercase letters, digits and inner hyphens, 3-30 chars.
        if (!preg_match('/^[a-z0-9](?:[a-z0-9-]{1,28})[a-z0-9]$/', $subdomain) || str_contains($subdomain, '--')) {
            throw new ValidationError('subdomain must be 3-30 chars of a-z, 0-9 and single inner hyphens');
        }
        if (in_array($subdomain, self::RESERVED, true)) {
            throw new ValidationError("subdomain '{$subdomain}' is reserved");
        }
    }

    /**
     * Sign-up: register the tenant, create its database, run the tenant migrations,
     * seed the default roles, and create the first user as admin.
     */
    public function create(string $subdomain, string $orgName, string $adminName, string $adminEmail, string $adminPassword): Tenant
    {
        $subdomain = strtolower(trim($subdomain));
        self::validateSubdomain($subdomain);
        if ($this->findBySubdomain($subdomain) !== null) {
            throw new ValidationError("subdomain '{$subdomain}' is taken");
        }
        Auth::validateCredentials($adminEmail, $adminPassword);

        // The file name is random, never derived from user input, so a subdomain
        // can't be used for path traversal and file names don't leak tenant names.
        $dbFile = bin2hex(random_bytes(8)) . '.sqlite';
        $dbPath = $this->storageDir . '/tenants/' . $dbFile;

        try {
            $tenantDb = Db::open($dbPath);
            Migrator::run($tenantDb, Migrator::tenant());
            (new Rbac($tenantDb))->seedDefaults();
            $adminId = (new Auth($tenantDb))->register($adminName, $adminEmail, $adminPassword);
            (new Rbac($tenantDb))->assignRole($adminId, 'admin');
            unset($tenantDb);

            $stmt = $this->system->prepare(
                'INSERT INTO tenants (subdomain, name, db_file, created_at) VALUES (?, ?, ?, ?)'
            );
            $stmt->execute([$subdomain, trim($orgName), $dbFile, gmdate('c')]);
        } catch (\Throwable $e) {
            // Don't leave an orphaned half-built database behind.
            // Close the connection first: Windows refuses to delete an open file.
            $tenantDb = null;
            @unlink($dbPath);
            if ($e instanceof \PDOException && str_contains($e->getMessage(), 'UNIQUE')) {
                throw new ValidationError("subdomain '{$subdomain}' is taken");
            }
            throw $e;
        }

        return $this->findBySubdomain($subdomain);
    }

    public function findBySubdomain(string $subdomain): ?Tenant
    {
        $stmt = $this->system->prepare('SELECT * FROM tenants WHERE subdomain = ?');
        $stmt->execute([strtolower($subdomain)]);
        $row = $stmt->fetch();
        return $row ? $this->hydrate($row) : null;
    }

    /**
     * Identify the tenant from the request's Host header: "acme.example.test:8080"
     * resolves to tenant "acme". The bare base domain, unknown subdomains and
     * nested subdomains all resolve to null.
     */
    public function resolveHost(string $host): ?Tenant
    {
        $host = strtolower(preg_replace('/:\d+$/', '', trim($host)));
        $suffix = '.' . strtolower($this->baseDomain);
        if (!str_ends_with($host, $suffix)) {
            return null;
        }
        $label = substr($host, 0, -strlen($suffix));
        if ($label === '' || str_contains($label, '.')) {
            return null;
        }
        return $this->findBySubdomain($label);
    }

    /** @return list<Tenant> */
    public function all(): array
    {
        $rows = $this->system->query('SELECT * FROM tenants ORDER BY id')->fetchAll();
        return array_map(fn (array $r) => $this->hydrate($r), $rows);
    }

    /** Re-run tenant migrations on every tenant (after adding a new migration). */
    public function migrateAll(): int
    {
        $total = 0;
        foreach ($this->all() as $tenant) {
            $total += Migrator::run($tenant->db(), Migrator::tenant());
        }
        return $total;
    }

    public function delete(string $subdomain): bool
    {
        $tenant = $this->findBySubdomain($subdomain);
        if ($tenant === null) {
            return false;
        }
        $this->system->prepare('DELETE FROM tenants WHERE id = ?')->execute([$tenant->id]);
        @unlink($tenant->dbPath);
        return true;
    }

    private function hydrate(array $row): Tenant
    {
        return new Tenant(
            (int) $row['id'],
            $row['subdomain'],
            $row['name'],
            $this->storageDir . '/tenants/' . $row['db_file'],
        );
    }
}
