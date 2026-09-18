@extends('layouts.app')

@section('title', 'Add a book')

@section('content')
    <h1>Add a book</h1>

    <form method="POST" action="{{ route('books.store') }}">
        @csrf
        @include('books._form')
        <button type="submit">Save</button>
    </form>
@endsection
