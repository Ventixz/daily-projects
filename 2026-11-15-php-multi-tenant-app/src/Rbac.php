<?php
declare(strict_types=1);

namespace Tenancy;

use PDO;

/**
 * Role-based access control inside one tenant. Users have roles, roles have
 * permissions, and code only ever asks "can this user do X?", never "is this
 * user an admin?", so roles can be changed without touching the checks.
 */
final class Rbac
{
    /** Default roles seeded into every new tenant. */
    public const DEFAULTS = [
        'admin' => ['invite users', 'manage roles', 'view projects', 'create projects', 'delete projects'],
        'member' => ['view projects', 'create projects'],
        'viewer' => ['view projects'],
    ];

    /** @var array<int, array<string, true>> per-request cache of user id => permission set */
    private array $cache = [];

    public function __construct(private readonly PDO $db)
    {
    }

    public function seedDefaults(): void
    {
        $this->db->beginTransaction();
        foreach (self::DEFAULTS as $role => $perms) {
            $this->db->prepare('INSERT OR IGNORE INTO roles (name) VALUES (?)')->execute([$role]);
            foreach ($perms as $perm) {
                $this->db->prepare('INSERT OR IGNORE INTO permissions (name) VALUES (?)')->execute([$perm]);
                $this->db->prepare(
                    'INSERT OR IGNORE INTO role_permissions (role_id, permission_id)
                     SELECT r.id, p.id FROM roles r, permissions p WHERE r.name = ? AND p.name = ?'
                )->execute([$role, $perm]);
            }
        }
        $this->db->commit();
    }

    public function roleId(string $role): int
    {
        $stmt = $this->db->prepare('SELECT id FROM roles WHERE name = ?');
        $stmt->execute([$role]);
        $id = $stmt->fetchColumn();
        if ($id === false) {
            throw new ValidationError("unknown role '{$role}'");
        }
        return (int) $id;
    }

    public function assignRole(int $userId, string $role): void
    {
        $this->db->prepare('INSERT OR IGNORE INTO user_roles (user_id, role_id) VALUES (?, ?)')
            ->execute([$userId, $this->roleId($role)]);
        unset($this->cache[$userId]);
    }

    /**
     * Remove a role. Refuses to remove the last admin: a tenant with no admin
     * could never invite anyone or change roles again.
     */
    public function removeRole(int $userId, string $role): void
    {
        $roleId = $this->roleId($role);
        if ($role === 'admin' && $this->hasRole($userId, 'admin') && $this->countWithRole('admin') <= 1) {
            throw new ValidationError('cannot remove the last admin');
        }
        $this->db->prepare('DELETE FROM user_roles WHERE user_id = ? AND role_id = ?')->execute([$userId, $roleId]);
        unset($this->cache[$userId]);
    }

    /** Change another user's role, as $actorId. Requires "manage roles". */
    public function setRole(int $actorId, int $userId, string $role): void
    {
        $this->authorize($actorId, 'manage roles');
        $this->roleId($role); // validate before changing anything
        $this->db->beginTransaction();
        try {
            foreach ($this->roles($userId) as $current) {
                if ($current !== $role) {
                    $this->removeRole($userId, $current);
                }
            }
            $this->assignRole($userId, $role);
            $this->db->commit();
        } catch (\Throwable $e) {
            $this->db->rollBack();
            unset($this->cache[$userId]);
            throw $e;
        }
    }

    public function hasRole(int $userId, string $role): bool
    {
        return in_array($role, $this->roles($userId), true);
    }

    /** @return list<string> */
    public function roles(int $userId): array
    {
        $stmt = $this->db->prepare(
            'SELECT r.name FROM roles r JOIN user_roles ur ON ur.role_id = r.id WHERE ur.user_id = ? ORDER BY r.name'
        );
        $stmt->execute([$userId]);
        return $stmt->fetchAll(PDO::FETCH_COLUMN);
    }

    /** @return list<string> */
    public function permissions(int $userId): array
    {
        return array_keys($this->permissionSet($userId));
    }

    public function can(int $userId, string $permission): bool
    {
        return isset($this->permissionSet($userId)[$permission]);
    }

    public function authorize(int $userId, string $permission): void
    {
        if (!$this->can($userId, $permission)) {
            throw new Forbidden("missing permission '{$permission}'");
        }
    }

    private function countWithRole(string $role): int
    {
        $stmt = $this->db->prepare(
            'SELECT COUNT(*) FROM user_roles ur JOIN roles r ON r.id = ur.role_id WHERE r.name = ?'
        );
        $stmt->execute([$role]);
        return (int) $stmt->fetchColumn();
    }

    /** @return array<string, true> */
    private function permissionSet(int $userId): array
    {
        if (!isset($this->cache[$userId])) {
            $stmt = $this->db->prepare(
                'SELECT DISTINCT p.name FROM permissions p
                 JOIN role_permissions rp ON rp.permission_id = p.id
                 JOIN user_roles ur ON ur.role_id = rp.role_id
                 WHERE ur.user_id = ? ORDER BY p.name'
            );
            $stmt->execute([$userId]);
            $this->cache[$userId] = array_fill_keys($stmt->fetchAll(PDO::FETCH_COLUMN), true);
        }
        return $this->cache[$userId];
    }
}
