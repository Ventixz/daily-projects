<?php
declare(strict_types=1);

namespace Tenancy;

use PDO;

/** Opens SQLite connections with the settings every connection in this app needs. */
final class Db
{
    public static function open(string $path): PDO
    {
        $pdo = new PDO('sqlite:' . $path, null, null, [
            PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES => false,
        ]);
        // SQLite ignores FOREIGN KEY clauses unless this is switched on per connection.
        $pdo->exec('PRAGMA foreign_keys = ON');
        $pdo->exec('PRAGMA busy_timeout = 3000');
        return $pdo;
    }
}
