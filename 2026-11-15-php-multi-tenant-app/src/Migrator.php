<?php
declare(strict_types=1);

namespace Tenancy;

use PDO;

/**
 * Runs an ordered list of named migrations once each, recording them in a
 * `migrations` table. Used for the system database and for every tenant database.
 */
final class Migrator
{
    /** @param array<string, string> $migrations name => SQL, applied in array order */
    public static function run(PDO $pdo, array $migrations): int
    {
        $pdo->exec('CREATE TABLE IF NOT EXISTS migrations (name TEXT PRIMARY KEY, ran_at TEXT NOT NULL)');
        $done = array_flip($pdo->query('SELECT name FROM migrations')->fetchAll(PDO::FETCH_COLUMN));
        $applied = 0;
        foreach ($migrations as $name => $sql) {
            if (isset($done[$name])) {
                continue;
            }
            $pdo->beginTransaction();
            try {
                $pdo->exec($sql);
                $pdo->prepare('INSERT INTO migrations (name, ran_at) VALUES (?, ?)')
                    ->execute([$name, gmdate('c')]);
                $pdo->commit();
            } catch (\Throwable $e) {
                $pdo->rollBack();
                throw $e;
            }
            $applied++;
        }
        return $applied;
    }

    /** Schema of the shared "system" database: which tenants exist and where their data lives. */
    public static function system(): array
    {
        return [
            '0001_create_tenants' => <<<SQL
                CREATE TABLE tenants (
                    id INTEGER PRIMARY KEY,
                    subdomain TEXT NOT NULL UNIQUE,
                    name TEXT NOT NULL,
                    db_file TEXT NOT NULL UNIQUE,
                    created_at TEXT NOT NULL
                );
            SQL,
        ];
    }

    /** Schema applied to each tenant's own database. Identical for all tenants. */
    public static function tenant(): array
    {
        return [
            '0001_create_users' => <<<SQL
                CREATE TABLE users (
                    id INTEGER PRIMARY KEY,
                    name TEXT NOT NULL,
                    email TEXT NOT NULL UNIQUE COLLATE NOCASE,
                    password_hash TEXT NOT NULL,
                    created_at TEXT NOT NULL
                );
            SQL,
            '0002_create_rbac' => <<<SQL
                CREATE TABLE roles (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);
                CREATE TABLE permissions (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE);
                CREATE TABLE role_permissions (
                    role_id INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
                    permission_id INTEGER NOT NULL REFERENCES permissions(id) ON DELETE CASCADE,
                    PRIMARY KEY (role_id, permission_id)
                );
                CREATE TABLE user_roles (
                    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                    role_id INTEGER NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
                    PRIMARY KEY (user_id, role_id)
                );
            SQL,
            '0003_create_invitations' => <<<SQL
                CREATE TABLE invitations (
                    id INTEGER PRIMARY KEY,
                    email TEXT NOT NULL COLLATE NOCASE,
                    role_id INTEGER NOT NULL REFERENCES roles(id),
                    token_hash TEXT NOT NULL UNIQUE,
                    invited_by INTEGER NOT NULL REFERENCES users(id),
                    expires_at INTEGER NOT NULL,
                    accepted_at INTEGER
                );
            SQL,
            '0004_create_projects' => <<<SQL
                CREATE TABLE projects (
                    id INTEGER PRIMARY KEY,
                    title TEXT NOT NULL,
                    owner_id INTEGER NOT NULL REFERENCES users(id),
                    created_at TEXT NOT NULL
                );
            SQL,
        ];
    }
}
