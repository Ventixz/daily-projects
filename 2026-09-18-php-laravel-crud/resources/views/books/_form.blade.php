@if ($errors->any())
    <div class="errors">
        <ul>
            @foreach ($errors->all() as $error)
                <li>{{ $error }}</li>
            @endforeach
        </ul>
    </div>
@endif

<div class="field">
    <label for="title">Title</label>
    <input type="text" id="title" name="title" value="{{ old('title', $book->title) }}">
</div>

<div class="field">
    <label for="author">Author</label>
    <input type="text" id="author" name="author" value="{{ old('author', $book->author) }}">
</div>

<div class="field">
    <label for="isbn">ISBN</label>
    <input type="text" id="isbn" name="isbn" value="{{ old('isbn', $book->isbn) }}">
</div>

<div class="field">
    <label for="status">Status</label>
    <select id="status" name="status">
        @foreach (['available' => 'Available', 'checked_out' => 'Checked out'] as $value => $label)
            <option value="{{ $value }}" @selected(old('status', $book->status) === $value)>{{ $label }}</option>
        @endforeach
    </select>
</div>

<div class="field">
    <label for="due_date">Due date (only while checked out)</label>
    <input
        type="date"
        id="due_date"
        name="due_date"
        value="{{ old('due_date', optional($book->due_date)->format('Y-m-d')) }}"
    >
</div>
