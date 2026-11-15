<?php
declare(strict_types=1);

// Tenant admin CLI.
//   php bin/tenant.php create <subdomain> "<org name>" "<admin name>" <admin email> <password>
//   php bin/tenant.php list
//   php bin/tenant.php migrate
//   php bin/tenant.php delete <subdomain>

require __DIR__ . '/../src/autoload.php';

use Tenancy\TenantManager;
use Tenancy\ValidationError;

$storage = getenv('TENANCY_STORAGE') ?: __DIR__ . '/../storage';
$domain = getenv('TENANCY_DOMAIN') ?: 'localtest.me';
$tm = new TenantManager($storage, $domain);

$cmd = $argv[1] ?? 'help';
try {
    switch ($cmd) {
        case 'create':
            if ($argc !== 7) {
                throw new ValidationError('usage: create <subdomain> <org> <admin name> <email> <password>');
            }
            $t = $tm->create($argv[2], $argv[3], $argv[4], $argv[5], $argv[6]);
            echo "created {$t->subdomain} -> http://{$t->subdomain}.{$domain}:8080\n";
            break;
        case 'list':
            foreach ($tm->all() as $t) {
                printf("%-20s %s\n", $t->subdomain, $t->name);
            }
            break;
        case 'migrate':
            echo 'applied ' . $tm->migrateAll() . " migration(s)\n";
            break;
        case 'delete':
            echo $tm->delete($argv[2] ?? '') ? "deleted\n" : "no such tenant\n";
            break;
        default:
            echo "commands: create, list, migrate, delete\n";
    }
} catch (ValidationError $e) {
    fwrite(STDERR, 'error: ' . $e->getMessage() . "\n");
    exit(1);
}
