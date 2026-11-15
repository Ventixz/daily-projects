<?php
declare(strict_types=1);

// Tiny PSR-4-style autoloader so the project runs with no Composer install.
spl_autoload_register(static function (string $class): void {
    $prefix = 'Tenancy\\';
    if (!str_starts_with($class, $prefix)) {
        return;
    }
    $file = __DIR__ . '/' . str_replace('\\', '/', substr($class, strlen($prefix))) . '.php';
    if (is_file($file)) {
        require $file;
    }
});
