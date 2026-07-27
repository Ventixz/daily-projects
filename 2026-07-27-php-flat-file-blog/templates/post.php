<?php
/** @var Post $post */
require __DIR__ . '/_header.php';
?>
<article>
    <h1><?= htmlspecialchars($post->title, ENT_QUOTES) ?></h1>
    <p class="date"><?= htmlspecialchars($post->date->format('F j, Y'), ENT_QUOTES) ?></p>
    <?= $post->renderedBody() ?>
</article>
<p><a href="/">&larr; back to all posts</a></p>
<?php require __DIR__ . '/_footer.php'; ?>
