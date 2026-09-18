@extends('layouts.app')

@section('title', 'Books')

@section('content')
    <h1>Books</h1>
    <p><a href="{{ route('books.create') }}">+ Add a book</a></p>

    @if ($books->isEmpty())
        <p>No books yet.</p>
    @else
        <table>
            <thead>
                <tr>
                    <th>Title</th>
                    <th>Author</th>
                    <th>Status</th>
                    <th>Due</th>
                </tr>
            </thead>
            <tbody>
                @foreach ($books as $book)
                    <tr>
                        <td><a href="{{ route('books.show', $book) }}">{{ $book->title }}</a></td>
                        <td>{{ $book->author }}</td>
                        <td>
                            <span class="status status-{{ $book->displayStatus() }}">
                                {{ ['available' => 'Available', 'checked_out' => 'Checked out', 'overdue' => 'Overdue'][$book->displayStatus()] }}
                            </span>
                        </td>
                        <td>{{ $book->due_date?->format('Y-m-d') ?? '—' }}</td>
                    </tr>
                @endforeach
            </tbody>
        </table>

        <nav class="pagination">{{ $books->links() }}</nav>
    @endif
@endsection
