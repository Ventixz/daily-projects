<?php

namespace Tests\Feature;

use App\Models\Book;
use Illuminate\Foundation\Testing\RefreshDatabase;
use Tests\TestCase;

class BookCrudTest extends TestCase
{
    use RefreshDatabase;

    public function test_index_lists_books(): void
    {
        Book::factory()->create(['title' => 'Dune']);

        $this->get(route('books.index'))
            ->assertOk()
            ->assertSee('Dune');
    }

    public function test_root_redirects_to_the_book_index(): void
    {
        $this->get('/')->assertRedirect(route('books.index'));
    }

    public function test_store_creates_an_available_book_without_a_due_date(): void
    {
        $response = $this->post(route('books.store'), [
            'title' => 'The Hobbit',
            'author' => 'J.R.R. Tolkien',
            'isbn' => '978-0345339683',
            'status' => 'available',
            'due_date' => '',
        ]);

        $book = Book::sole();
        $response->assertRedirect(route('books.show', $book));
        $this->assertSame('available', $book->status);
        $this->assertNull($book->due_date);
    }

    public function test_store_requires_a_due_date_when_checked_out(): void
    {
        $response = $this->post(route('books.store'), [
            'title' => 'The Hobbit',
            'author' => 'J.R.R. Tolkien',
            'isbn' => '978-0345339683',
            'status' => 'checked_out',
            'due_date' => '',
        ]);

        $response->assertSessionHasErrors('due_date');
        $this->assertSame(0, Book::count());
    }

    public function test_store_rejects_a_due_date_on_an_available_book(): void
    {
        $response = $this->post(route('books.store'), [
            'title' => 'The Hobbit',
            'author' => 'J.R.R. Tolkien',
            'isbn' => '978-0345339683',
            'status' => 'available',
            'due_date' => now()->addWeek()->toDateString(),
        ]);

        $response->assertSessionHasErrors('due_date');
        $this->assertSame(0, Book::count());
    }

    public function test_store_rejects_a_due_date_in_the_past(): void
    {
        $response = $this->post(route('books.store'), [
            'title' => 'The Hobbit',
            'author' => 'J.R.R. Tolkien',
            'isbn' => '978-0345339683',
            'status' => 'checked_out',
            'due_date' => now()->subDay()->toDateString(),
        ]);

        $response->assertSessionHasErrors('due_date');
        $this->assertSame(0, Book::count());
    }

    public function test_store_rejects_a_duplicate_isbn(): void
    {
        Book::factory()->create(['isbn' => '978-0345339683']);

        $response = $this->post(route('books.store'), [
            'title' => 'Another Copy',
            'author' => 'Someone',
            'isbn' => '978-0345339683',
            'status' => 'available',
            'due_date' => '',
        ]);

        $response->assertSessionHasErrors('isbn');
        $this->assertSame(1, Book::count());
    }

    public function test_update_allows_keeping_a_due_date_that_is_already_overdue(): void
    {
        $book = Book::factory()->checkedOut()->create([
            'due_date' => now()->subWeek(),
        ]);

        $response = $this->put(route('books.update', $book), [
            'title' => $book->title,
            'author' => $book->author,
            'isbn' => $book->isbn,
            'status' => 'checked_out',
            'due_date' => now()->subWeek()->toDateString(),
        ]);

        $response->assertRedirect(route('books.show', $book));
        $response->assertSessionDoesntHaveErrors();
    }

    public function test_update_allows_isbn_to_stay_the_same_on_the_same_book(): void
    {
        $book = Book::factory()->create(['isbn' => '978-0345339683']);

        $response = $this->put(route('books.update', $book), [
            'title' => 'Renamed',
            'author' => $book->author,
            'isbn' => '978-0345339683',
            'status' => 'available',
            'due_date' => '',
        ]);

        $response->assertSessionDoesntHaveErrors();
        $this->assertSame('Renamed', $book->fresh()->title);
    }

    public function test_marking_a_checked_out_book_available_again_clears_the_due_date_requirement(): void
    {
        $book = Book::factory()->checkedOut()->create();

        $response = $this->put(route('books.update', $book), [
            'title' => $book->title,
            'author' => $book->author,
            'isbn' => $book->isbn,
            'status' => 'available',
            'due_date' => '',
        ]);

        $response->assertSessionDoesntHaveErrors();
        $this->assertSame('available', $book->fresh()->status);
        $this->assertNull($book->fresh()->due_date);
    }

    public function test_destroy_removes_the_book(): void
    {
        $book = Book::factory()->create();

        $this->delete(route('books.destroy', $book))
            ->assertRedirect(route('books.index'));

        $this->assertSame(0, Book::count());
    }
}
