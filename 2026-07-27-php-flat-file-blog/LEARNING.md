# Flat-File Blog (PHP)

**Source:** [Make Your Own Blog (in Pure PHP)](http://ilovephp.jondh.me.uk/en/tutorial/make-your-own-blog),
from [practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

## What it is

A blog with no database and no framework: posts are Markdown files with a small front-matter
header, sitting in `posts/`. `Post::fromFile` splits the `---`-delimited front matter from the
body and parses it into a typed `Post`. `Blog` globs the directory, builds a `Post` per file, and
sorts newest-first by the front-matter date. `Router::match` turns a request URI into a route name
and params with no framework underneath it — just `parse_url` and two `preg_match` calls.
`index.php` is the front controller PHP's built-in server invokes for every request; it wires
router output to one of three plain-PHP templates. `Markdown::toHtml` is a hand-rolled renderer
covering headers, paragraphs, lists, bold/italic/code spans, and links — enough of the syntax to
write real posts, not a CommonMark-compliant implementation.

## Run it

```bash
cd 2026-07-27-php-flat-file-blog
php tests/run_all.php               # 14 tests, no Composer/PHPUnit needed
php -S localhost:8000 index.php     # then visit http://localhost:8000
```

## What it actually teaches

- **A directory is a database if all you need is "list, sorted" and "find by key."** `Blog`
  has no index, no cache, no query language — `glob('*.md')` plus `usort` by front-matter date
  gives the homepage its order, and a linear scan over an array gives `findBySlug` its lookup.
  That's a real tradeoff (O(n) on every request, no concurrent-write safety), not a toy one, and
  it's the right tradeoff for a single-author blog with a few dozen posts.
- **Escaping has to happen before markup is generated, not after.** `Markdown::inline` calls
  `htmlspecialchars()` on the raw line *first*, then runs `preg_replace` to wrap pieces of the
  now-escaped text in `<strong>`/`<em>`/`<code>`/`<a>`. If a post body contains `<script>`, it's
  already inert text (`&lt;script&gt;`) before any tag-generating regex sees it, so there's no
  path from "text a post author typed" to "a tag the browser executes." Reversing those two
  steps — markup first, escape second — would `htmlspecialchars()` the very tags the renderer
  just created, and reversing them the *other* way (escape after generating tags from raw input)
  is the classic stored-XSS bug.
- **PHP's built-in dev server needs a router script specifically so it can special-case static
  files.** Every request — `/`, `/post/x`, `/style.css` — hits `index.php` first. The
  `PHP_SAPI === 'cli-server'` branch checks whether the requested path is a real file on disk and
  returns `false` if so, which tells the built-in server "serve this yourself, unmodified,"
  instead of letting PHP interpret `style.css` as a page to render. `nginx`/`fpm` deployments don't
  need this at all since the web server's own file check happens before PHP is invoked.
- **Splitting HTML into `_header.php`/`_footer.php` and `require`-ing them from three page
  templates is what a templating engine's "layout" concept boils down to at the language level.**
  There's no `{% extends %}` — just an ordinary file include before and after each page's unique
  content, using PHP itself as the template language (`<?= ?>` for escaped output).
- **The front-matter parser only recognizes what it explicitly looks for.** `splitFrontMatter`
  regex-matches `---\n...\n---\n` and treats every `key: value` line inside as metadata; a post
  missing the block entirely still works (`Post::fromFile`'s test for that), because everything
  degrades to sane defaults (slug as title, epoch as date) rather than throwing.

## What I'd add next (stretch goals I skipped for scope)

- Pagination once the linear `glob()` scan and full-directory `usort` stop being free — first
  real edge case to hit if this blog outgrew "toy" scale.
- A file-modification-time cache so `Blog` doesn't re-read and re-parse every Markdown file on
  every single request.
- Draft posts (a `draft: true` front-matter flag that `Blog::loadPosts` filters out of `allPosts`
  but a direct slug lookup can still preview).
- Nested/multi-line front-matter values and a real YAML parser, instead of the current
  one-`key: value`-per-line assumption.
