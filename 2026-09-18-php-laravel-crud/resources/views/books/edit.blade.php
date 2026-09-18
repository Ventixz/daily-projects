@extends('layouts.app')

@section('title', "Edit {$book->title}")

@section('content')
    <h1>Edit {{ $book->title }}</h1>

    <form method="POST" action="{{ route('books.update', $book) }}">
        @csrf
        @method('PUT')
        @include('books._form')
        <button type="submit">Save changes</button>
    </form>

    <form method="POST" action="{{ route('books.destroy', $book) }}" class="inline" onsubmit="return confirm('Remove this book?');">
        @csrf
        @method('DELETE')
        <button type="submit">Delete</button>
    </form>
@endsection
