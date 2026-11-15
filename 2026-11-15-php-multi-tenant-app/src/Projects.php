<?php
declare(strict_types=1);

namespace Tenancy;

use PDO;

/** A small tenant-owned resource, used to show permission checks guarding real data. */
final class Projects
{
    public function __construct(private readonly PDO $db, private readonly Rbac $rbac)
    {
    }

    public function create(int $userId, string $title): int
    {
        $this->rbac->authorize($userId, 'create projects');
        $title = trim($title);
        if ($title === '' || mb_strlen($title) > 120) {
            throw new ValidationError('title must be 1-120 characters');
        }
        $this->db->prepare('INSERT INTO projects (title, owner_id, created_at) VALUES (?, ?, ?)')
            ->execute([$title, $userId, gmdate('c')]);
        return (int) $this->db->lastInsertId();
    }

    /** @return list<array{id:int,title:string,owner:string}> */
    public function all(int $userId): array
    {
        $this->rbac->authorize($userId, 'view projects');
        $rows = $this->db->query(
            'SELECT p.id, p.title, u.name AS owner FROM projects p JOIN users u ON u.id = p.owner_id ORDER BY p.id'
        )->fetchAll();
        return array_map(static fn (array $r) => ['id' => (int) $r['id'], 'title' => $r['title'], 'owner' => $r['owner']], $rows);
    }

    public function delete(int $userId, int $projectId): bool
    {
        $this->rbac->authorize($userId, 'delete projects');
        $stmt = $this->db->prepare('DELETE FROM projects WHERE id = ?');
        $stmt->execute([$projectId]);
        return $stmt->rowCount() === 1;
    }
}
