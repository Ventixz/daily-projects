<?php
declare(strict_types=1);

namespace Tenancy;

use PDO;

/**
 * Users and passwords for one tenant. Because each tenant has its own users
 * table, "alice@x.com" at tenant A and at tenant B are two unrelated accounts
 * with separate passwords, and logging in at A can never find B's user.
 */
final class Auth
{
    public function __construct(private readonly PDO $db)
    {
    }

    public static function validateCredentials(string $email, string $password): void
    {
        if (filter_var($email, FILTER_VALIDATE_EMAIL) === false) {
            throw new ValidationError('invalid email');
        }
        if (strlen($password) < 8) {
            throw new ValidationError('password must be at least 8 characters');
        }
    }

    public function register(string $name, string $email, string $password): int
    {
        self::validateCredentials($email, $password);
        if (trim($name) === '') {
            throw new ValidationError('name is required');
        }
        try {
            $this->db->prepare('INSERT INTO users (name, email, password_hash, created_at) VALUES (?, ?, ?, ?)')
                ->execute([trim($name), strtolower(trim($email)), password_hash($password, PASSWORD_DEFAULT), gmdate('c')]);
        } catch (\PDOException $e) {
            if (str_contains($e->getMessage(), 'UNIQUE')) {
                throw new ValidationError('email already registered');
            }
            throw $e;
        }
        return (int) $this->db->lastInsertId();
    }

    /** @return array{id:int,name:string,email:string}|null */
    public function attempt(string $email, string $password): ?array
    {
        $stmt = $this->db->prepare('SELECT id, name, email, password_hash FROM users WHERE email = ?');
        $stmt->execute([strtolower(trim($email))]);
        $row = $stmt->fetch();
        if ($row === false) {
            // Still pay for a hash check so "no such user" and "wrong password"
            // take about the same time and can't be told apart by timing.
            password_verify($password, '$2y$10$usesomesillystringfore7hnbRJHxXVLeakoG8K30oukPsA.ztMG');
            return null;
        }
        if (!password_verify($password, $row['password_hash'])) {
            return null;
        }
        if (password_needs_rehash($row['password_hash'], PASSWORD_DEFAULT)) {
            $this->db->prepare('UPDATE users SET password_hash = ? WHERE id = ?')
                ->execute([password_hash($password, PASSWORD_DEFAULT), $row['id']]);
        }
        return ['id' => (int) $row['id'], 'name' => $row['name'], 'email' => $row['email']];
    }

    /** @return array{id:int,name:string,email:string}|null */
    public function find(int $id): ?array
    {
        $stmt = $this->db->prepare('SELECT id, name, email FROM users WHERE id = ?');
        $stmt->execute([$id]);
        $row = $stmt->fetch();
        return $row ? ['id' => (int) $row['id'], 'name' => $row['name'], 'email' => $row['email']] : null;
    }

    public function findIdByEmail(string $email): ?int
    {
        $stmt = $this->db->prepare('SELECT id FROM users WHERE email = ?');
        $stmt->execute([strtolower(trim($email))]);
        $id = $stmt->fetchColumn();
        return $id === false ? null : (int) $id;
    }
}
