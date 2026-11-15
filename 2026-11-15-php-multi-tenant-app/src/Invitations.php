<?php
declare(strict_types=1);

namespace Tenancy;

use PDO;

/**
 * Invite-only membership. An admin invites an email address with a role; the
 * invitee gets a link containing a random token and sets their own password.
 * Only a SHA-256 hash of the token is stored, so a leaked database can't be
 * used to accept pending invitations.
 */
final class Invitations
{
    public const TTL_SECONDS = 72 * 3600;

    /** @var \Closure(): int */
    private \Closure $clock;

    public function __construct(private readonly PDO $db, private readonly Rbac $rbac, ?\Closure $clock = null)
    {
        $this->clock = $clock ?? static fn (): int => time();
    }

    /** Returns the plaintext token. It is shown once (put in the email link) and never stored. */
    public function invite(int $inviterId, string $email, string $role): string
    {
        $this->rbac->authorize($inviterId, 'invite users');
        $email = strtolower(trim($email));
        if (filter_var($email, FILTER_VALIDATE_EMAIL) === false) {
            throw new ValidationError('invalid email');
        }
        $roleId = $this->rbac->roleId($role);
        if ((new Auth($this->db))->findIdByEmail($email) !== null) {
            throw new ValidationError('that email is already a member');
        }

        $now = ($this->clock)();
        $token = bin2hex(random_bytes(32));
        $this->db->beginTransaction();
        // Re-inviting replaces any pending invitation, so only the newest link works.
        $this->db->prepare('DELETE FROM invitations WHERE email = ? AND accepted_at IS NULL')->execute([$email]);
        $this->db->prepare(
            'INSERT INTO invitations (email, role_id, token_hash, invited_by, expires_at) VALUES (?, ?, ?, ?, ?)'
        )->execute([$email, $roleId, hash('sha256', $token), $inviterId, $now + self::TTL_SECONDS]);
        $this->db->commit();
        return $token;
    }

    /** @return array{email:string,role:string}|null a still-valid invitation, for showing the sign-up form */
    public function peek(string $token): ?array
    {
        $row = $this->findValid($token);
        return $row ? ['email' => $row['email'], 'role' => $row['role']] : null;
    }

    /** Accept: creates the user with the invited role. Returns the new user id. */
    public function accept(string $token, string $name, string $password): int
    {
        $this->db->beginTransaction();
        try {
            $row = $this->findValid($token);
            if ($row === null) {
                throw new ValidationError('invitation is invalid, expired or already used');
            }
            // Mark used first, guarded on accepted_at IS NULL, so two concurrent
            // accepts of the same link can't both succeed.
            $stmt = $this->db->prepare('UPDATE invitations SET accepted_at = ? WHERE id = ? AND accepted_at IS NULL');
            $stmt->execute([($this->clock)(), $row['id']]);
            if ($stmt->rowCount() !== 1) {
                throw new ValidationError('invitation is invalid, expired or already used');
            }
            $userId = (new Auth($this->db))->register($name, $row['email'], $password);
            $this->rbac->assignRole($userId, $row['role']);
            $this->db->commit();
            return $userId;
        } catch (\Throwable $e) {
            $this->db->rollBack();
            throw $e;
        }
    }

    /** @return list<array{email:string,role:string,expires_at:int}> */
    public function pending(): array
    {
        $stmt = $this->db->prepare(
            'SELECT i.email, r.name AS role, i.expires_at FROM invitations i JOIN roles r ON r.id = i.role_id
             WHERE i.accepted_at IS NULL AND i.expires_at > ? ORDER BY i.id'
        );
        $stmt->execute([($this->clock)()]);
        return array_map(
            static fn (array $r) => ['email' => $r['email'], 'role' => $r['role'], 'expires_at' => (int) $r['expires_at']],
            $stmt->fetchAll()
        );
    }

    private function findValid(string $token): ?array
    {
        if (!preg_match('/^[0-9a-f]{64}$/', $token)) {
            return null;
        }
        $stmt = $this->db->prepare(
            'SELECT i.id, i.email, r.name AS role FROM invitations i JOIN roles r ON r.id = i.role_id
             WHERE i.token_hash = ? AND i.accepted_at IS NULL AND i.expires_at > ?'
        );
        $stmt->execute([hash('sha256', $token), ($this->clock)()]);
        $row = $stmt->fetch();
        return $row ?: null;
    }
}
