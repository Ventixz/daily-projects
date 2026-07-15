Pick today's practice project and scaffold it. You are running headlessly in a GitHub Actions runner, checked out at the repo root. Write files only — do not run installers or package managers.

## Inputs already on disk

- `/tmp/pbl-readme.md` — the full README of practical-tutorials/project-based-learning, fetched fresh this run. Read it. Do not fetch it yourself.
- `HISTORY.md` — every project served so far. Read it before picking.
- `TODAY` — the environment variable holding today's date as `YYYY-MM-DD`. It is also written to `/tmp/today.txt`. Read that file; do not guess the date.

## Step 1: Pick

The README has ~22 language sections (C/C++, C#, Clojure, Dart/Flutter, Elixir, Erlang, F#, Go, Haskell, HTML/CSS, Java, JavaScript, Kotlin, Lua, OCaml, PHP, Python, R, Ruby, Rust, Scala, Swift) and ~230 tutorial links. Any language is fair game — variety is the point.

Rules:
- Never repeat a project already in HISTORY.md.
- Don't reuse any language from the last 7 entries in HISTORY.md.
- Target basic-to-intermediate scope: buildable in roughly 2-4 hours. Skip anything that reads like a multi-week series, an OS kernel, a full compiler, or a full-stack production clone. A "Part 1 of 6" entry qualifies only if Part 1 alone is a standalone deliverable.
- Skip entries marked "(outdated)" or "(work in progress)".
- Prefer projects that build something that runs and does something visible over pure-reading walkthroughs.
- Genuinely randomize. Do not gravitate to the top of the list, and do not drift toward Python or JavaScript because they are familiar. Pick a random section first, then a random qualifying entry within it. If the last several HISTORY.md entries skew one way (all web, all CLI, all scripting), deliberately swing the other way.

## Step 2: Scaffold

Create `<TODAY>-<short-project-slug>/` at the repo root.

**`BRIEF.md`** inside it, with these sections:

- **Project** — name, language, link to the source tutorial.
- **What you're building** — 2-3 sentences, plain English, describing the finished thing.
- **Why it's worth building** — the one or two concepts this project actually teaches (e.g. "recursive descent parsing", "how TCP framing works", "the event loop"). Be specific; not "you'll learn Go".
- **Setup** — exact commands to install the toolchain and initialize the project on **Windows 11 / PowerShell**. Use winget or scoop for toolchains that aren't typically preinstalled. Verify the version-check command is correct for that toolchain.
- **Milestones** — 3 to 5 ordered checkboxes. Each must be a runnable increment where something observable works. Not "write the parser" but "parse a single expression and print the AST".
- **Stretch goals** — 2 optional extras.
- **Hints** — the 1-2 places this project usually trips people up, and what to do about it. Omit this section entirely rather than inventing a failure mode you don't actually know.

**Skeleton** at the project root: the idiomatic entry point and dependency manifest for that language (e.g. `main.go` + `go.mod`, `src/main.rs` + `Cargo.toml`, `main.py` + `requirements.txt`, `index.html` + `script.js`). Minimal and runnable — a hello-world that executes, not stubs full of TODOs. **Do not implement the project here.** The developer writes this part.

**`reference/`** subfolder: a complete, working implementation of the project. This is the comparison solution, read only after the developer has attempted it themselves. Put a one-line header comment at the top of each reference file marking it as the AI-written reference. Add `reference/README.md` saying: this is a reference implementation, written by the daily-project bot, meant for comparison after your own attempt.

## Step 3: Log

Append one row to `HISTORY.md`:

`| <TODAY> | <Language> | <Project name> | new |`

## Step 4: Summarize

Write a two-line summary to `/tmp/summary.txt`: the project name and language on the first line, one sentence on what it builds on the second. The workflow uses this as the commit message body.

If the README is unreadable or a picked tutorial's URL is obviously dead, pick a different entry rather than failing the run.
