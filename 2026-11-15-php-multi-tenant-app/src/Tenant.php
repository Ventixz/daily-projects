<?php
declare(strict_types=1);

namespace Tenancy;

use PDO;

/** One tenant (an organisation) and a lazily opened connection to its private database. */
final class Tenant
{
    private ?PDO $pdo = null;

    public function __construct(
        public readonly int $id,
        public readonly string $subdomain,
        public readonly string $name,
        public readonly string $dbPath,
    ) {
    }

    public function db(): PDO
    {
        return $this->pdo ??= Db::open($this->dbPath);
    }

    public function rbac(): Rbac
    {
        return new Rbac($this->db());
    }

    public function auth(): Auth
    {
        return new Auth($this->db());
    }

    public function invitations(): Invitations
    {
        return new Invitations($this->db(), $this->rbac());
    }

    public function projects(): Projects
    {
        return new Projects($this->db(), $this->rbac());
    }
}
