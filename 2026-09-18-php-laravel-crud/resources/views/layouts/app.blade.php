<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>@yield('title', 'Books') &middot; Library</title>
    <style>
        body { font-family: system-ui, sans-serif; max-width: 720px; margin: 2rem auto; padding: 0 1rem; color: #1f2933; }
        table { width: 100%; border-collapse: collapse; margin-top: 1rem; }
        th, td { text-align: left; padding: 0.5rem; border-bottom: 1px solid #e4e7eb; }
        .status { display: inline-block; padding: 0.15rem 0.5rem; border-radius: 999px; font-size: 0.8rem; }
        .status-available { background: #d9f2e6; color: #12684a; }
        .status-checked_out { background: #fde7d9; color: #9a4a1a; }
        .status-overdue { background: #fbd5d5; color: #9b1c1c; }
        form.inline { display: inline; }
        .field { margin-bottom: 1rem; }
        label { display: block; font-weight: 600; margin-bottom: 0.25rem; }
        input, select { padding: 0.4rem; width: 100%; max-width: 320px; box-sizing: border-box; }
        .errors { background: #fbd5d5; color: #9b1c1c; padding: 0.75rem; border-radius: 4px; margin-bottom: 1rem; }
        .status-message { background: #d9f2e6; color: #12684a; padding: 0.5rem 0.75rem; border-radius: 4px; margin-bottom: 1rem; }
        .actions a, .actions button { margin-right: 0.5rem; }
        nav.pagination { margin-top: 1rem; }
    </style>
</head>
<body>
    <p><a href="{{ route('books.index') }}">&larr; All books</a></p>

    @if (session('status'))
        <div class="status-message">{{ session('status') }}</div>
    @endif

    @yield('content')
</body>
</html>
