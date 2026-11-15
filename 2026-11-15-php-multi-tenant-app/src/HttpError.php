<?php
declare(strict_types=1);

namespace Tenancy;

/** An error with an explicit HTTP status code. */
final class HttpError extends \RuntimeException
{
    public function __construct(string $message, int $status)
    {
        parent::__construct($message, $status);
    }
}
