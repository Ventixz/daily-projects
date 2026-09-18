@extends('layouts.app')

@section('title', $book->title)

@section('content')
    <h1>{{ $book->title }}</h1>
    <p>by {{ $book->author }} &middot; ISBN {{ $book->isbn }}</p>

    <p>
        <span class="status status-{{ $book->displayStatus() }}">
            {{ ['available' => 'Available', 'checked_out' => 'Checked out', 'overdue' => 'Overdue'][$book->displayStatus()] }}
        </span>
        @if ($book->due_date)
            &mdash; due {{ $book->due_date->format('Y-m-d') }}
        @endif
    </p>

    <p class="actions">
        <a href="{{ route('books.edit', $book) }}">Edit</a>
        <form method="POST" action="{{ route('books.destroy', $book) }}" class="inline" onsubmit="return confirm('Remove this book?');">
            @csrf
            @method('DELETE')
            <button type="submit">Delete</button>
        </form>
    </p>
@endsection
