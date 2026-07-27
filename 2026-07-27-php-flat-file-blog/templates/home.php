<?php
/** @var list<Post> $posts */
require __DIR__ . '/_header.php';
?>
<ul class="post-list">
<?php foreach ($posts as $post): ?>
    <li>
        <a href="/post/<?= htmlspecialchars($post->slug, ENT_QUOTES) ?>"><?= htmlspecialchars($post->title, ENT_QUOTES) ?></a>
        <span class="date"><?= htmlspecialchars($post->date->format('F j, Y'), ENT_QUOTES) ?></span>
    </li>
<?php endforeach; ?>
</ul>
<?php require __DIR__ . '/_footer.php'; ?>
