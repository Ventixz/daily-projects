<?php

namespace Tests\Unit;

use App\Models\Book;
use Tests\TestCase;

class BookTest extends TestCase
{
    public function test_available_book_is_never_overdue_even_with_a_past_due_date(): void
    {
        $book = new Book([
            'status' => 'available',
            'due_date' => now()->subWeek(),
        ]);

        $this->assertFalse($book->isOverdue());
        $this->assertSame('available', $book->displayStatus());
    }

    public function test_checked_out_book_is_overdue_once_its_due_date_has_passed(): void
    {
        $book = new Book([
            'status' => 'checked_out',
            'due_date' => now()->subDay(),
        ]);

        $this->assertTrue($book->isOverdue());
        $this->assertSame('overdue', $book->displayStatus());
    }

    public function test_checked_out_book_is_not_overdue_before_its_due_date(): void
    {
        $book = new Book([
            'status' => 'checked_out',
            'due_date' => now()->addDay(),
        ]);

        $this->assertFalse($book->isOverdue());
        $this->assertSame('checked_out', $book->displayStatus());
    }
}
