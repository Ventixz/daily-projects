<?php
declare(strict_types=1);

namespace Tenancy;

/** The user is authenticated but lacks the permission (maps to HTTP 403). */
final class Forbidden extends \RuntimeException
{
}
