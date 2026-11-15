<?php
declare(strict_types=1);

namespace Tenancy;

/** Bad input from the client (maps to HTTP 422). */
final class ValidationError extends \RuntimeException
{
}
