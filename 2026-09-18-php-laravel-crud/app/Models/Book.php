<?php

namespace App\Models;

use Database\Factories\BookFactory;
use Illuminate\Database\Eloquent\Factories\HasFactory;
use Illuminate\Database\Eloquent\Model;

class Book extends Model
{
    /** @use HasFactory<BookFactory> */
    use HasFactory;

    protected $fillable = [
        'title',
        'author',
        'isbn',
        'status',
        'due_date',
    ];

    protected function casts(): array
    {
        return [
            'due_date' => 'date',
        ];
    }

    /**
     * A book is overdue only while it's actually checked out and its due
     * date has passed - an available book with a stale due_date left over
     * from a prior loan should never show as overdue.
     */
    public function isOverdue(): bool
    {
        return $this->status === 'checked_out'
            && $this->due_date !== null
            && $this->due_date->isPast();
    }

    public function displayStatus(): string
    {
        if ($this->isOverdue()) {
            return 'overdue';
        }

        return $this->status === 'checked_out' ? 'checked_out' : 'available';
    }
}
