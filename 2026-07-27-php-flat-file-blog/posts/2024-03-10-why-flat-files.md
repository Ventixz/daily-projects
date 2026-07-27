---
title: Why Flat Files
date: 2024-03-10
---
A blog is mostly a **sorted list** and a **lookup by slug** — both of which a
directory of files already gives you for free. `glob()` finds every post,
the filename doubles as a stable slug, and the date in the front matter (not
the filesystem mtime, which `git clone` and backups don't preserve) decides
the order.

The tradeoff is real: no transactions, no concurrent-write safety, and a
linear scan through every post on every page load. For a single-author blog
serving a handful of pages, that scan is microseconds, and the simplicity of
`cat posts/*.md` being the entire backup procedure is worth more than the
throughput a database would buy back.
