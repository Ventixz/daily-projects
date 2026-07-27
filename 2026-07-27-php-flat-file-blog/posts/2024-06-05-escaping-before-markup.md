---
title: Escaping Before Markup
date: 2024-06-05
---
The `Markdown` renderer here does exactly one security-relevant thing:
it calls `htmlspecialchars()` on a line **before** any bold, italic, code, or
link syntax is turned into real HTML tags.

If that order were reversed, a post body containing `<script>` would already
be a live `<script>` tag by the time bold and italic markers were applied,
and there'd be no way to tell "HTML the renderer generated" from "HTML a post
author typed" apart afterwards. Escaping first means every angle bracket
that survives into the final output is one this renderer put there on
purpose — `<strong>`, `<em>`, `<code>`, `<a href>`, and nothing else.
