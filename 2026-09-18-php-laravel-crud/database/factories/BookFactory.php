<?php

namespace Database\Factories;

use App\Models\Book;
use Illuminate\Database\Eloquent\Factories\Factory;

/**
 * @extends Factory<Book>
 */
class BookFactory extends Factory
{
    /**
     * Define the model's default state.
     *
     * @return array<string, mixed>
     */
    public function definition(): array
    {
        return [
            'title' => fake()->sentence(3),
            'author' => fake()->name(),
            'isbn' => fake()->unique()->isbn13(),
            'status' => 'available',
            'due_date' => null,
        ];
    }

    /**
     * Indicate that the book is checked out, with a due date required to go with it.
     */
    public function checkedOut(): static
    {
        return $this->state(fn () => [
            'status' => 'checked_out',
            'due_date' => now()->addWeeks(2)->toDateString(),
        ]);
    }
}
