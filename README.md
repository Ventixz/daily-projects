# daily-projects

A practice project every day, drawn at random from
[practical-tutorials/project-based-learning](https://github.com/practical-tutorials/project-based-learning).

## How this repo works, honestly

**The briefs and scaffolds in this repo are generated automatically.** A scheduled
routine runs each day at 1:00 PM, picks one project from the list, and commits a
dated folder containing:

- `BRIEF.md` — what to build, what it teaches, setup steps, milestones
- a minimal runnable skeleton for that language (a hello-world that executes)
- `reference/` — a working implementation, also AI-written

Those commits are authored by `daily-project-routine`, because a bot wrote them.

**The implementations under each project root are mine.** That's the point of the
repo — the brief tells me what to build, and I build it. Those commits are authored
by me, and they're the only ones that represent my own work.

So: if you're reading this repo to judge what I can do, look at the project code and
ignore the scaffolding. The split is visible in the commit history.

## Layout

```
2026-07-16-http-server/
├── BRIEF.md          # bot-written: the spec
├── main.go           # bot-written skeleton → my implementation
├── go.mod
└── reference/        # bot-written: a working solution, for comparison after I attempt it
```

`HISTORY.md` is the log of every project served, and the routine reads it before
picking so it never repeats a project or reuses a language from the last 7 days.

## Ground rule for the reference folder

Don't open `reference/` until I've made a real attempt. It exists to compare
approaches afterward, not to copy from. If I read it first the day is wasted.
