//! Pure application state: no DOM, no wasm-bindgen, compiles and tests on any target.
//! `dom.rs` is the only module that knows a browser exists; everything here is just
//! data transformations, so `cargo test` (native, no wasm32 toolchain needed) covers
//! the actual logic while the wasm side stays a thin, mostly-untestable adapter.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Filter {
    All,
    Active,
    Completed,
}

impl Filter {
    pub fn from_hash(hash: &str) -> Filter {
        match hash.trim_start_matches('#') {
            "/active" => Filter::Active,
            "/completed" => Filter::Completed,
            _ => Filter::All,
        }
    }

    pub fn as_hash(self) -> &'static str {
        match self {
            Filter::All => "#/",
            Filter::Active => "#/active",
            Filter::Completed => "#/completed",
        }
    }

    fn matches(self, done: bool) -> bool {
        match self {
            Filter::All => true,
            Filter::Active => !done,
            Filter::Completed => done,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Todo {
    pub id: u32,
    pub text: String,
    pub done: bool,
}

#[derive(Debug, Clone)]
pub struct AppState {
    pub todos: Vec<Todo>,
    pub filter: Filter,
    next_id: u32,
}

impl Default for AppState {
    fn default() -> Self {
        Self::new()
    }
}

impl AppState {
    pub fn new() -> Self {
        AppState {
            todos: Vec::new(),
            filter: Filter::All,
            next_id: 1,
        }
    }

    /// Returns false (and adds nothing) for blank input, mirroring TodoMVC's rule
    /// that whitespace-only entries never become a todo.
    pub fn add(&mut self, text: &str) -> bool {
        let trimmed = text.trim();
        if trimmed.is_empty() {
            return false;
        }
        let id = self.next_id;
        self.next_id += 1;
        self.todos.push(Todo {
            id,
            text: trimmed.to_string(),
            done: false,
        });
        true
    }

    pub fn toggle(&mut self, id: u32) {
        if let Some(todo) = self.todos.iter_mut().find(|t| t.id == id) {
            todo.done = !todo.done;
        }
    }

    pub fn remove(&mut self, id: u32) {
        self.todos.retain(|t| t.id != id);
    }

    pub fn clear_completed(&mut self) {
        self.todos.retain(|t| !t.done);
    }

    pub fn set_filter(&mut self, filter: Filter) {
        self.filter = filter;
    }

    pub fn visible(&self) -> impl Iterator<Item = &Todo> {
        self.todos
            .iter()
            .filter(move |t| self.filter.matches(t.done))
    }

    pub fn remaining_count(&self) -> usize {
        self.todos.iter().filter(|t| !t.done).count()
    }

    /// Tab/newline-delimited, not JSON: this project's only "wire format" is
    /// localStorage, so it doesn't need a real parser, just something round-trippable.
    /// Field text is escaped so a todo containing a literal tab or newline can't
    /// corrupt the row structure.
    pub fn serialize(&self) -> String {
        self.todos
            .iter()
            .map(|t| format!("{}\t{}\t{}", t.id, t.done, escape(&t.text)))
            .collect::<Vec<_>>()
            .join("\n")
    }

    pub fn deserialize(data: &str) -> AppState {
        let mut state = AppState::new();
        for line in data.lines() {
            let mut parts = line.splitn(3, '\t');
            let (Some(id), Some(done), Some(text)) = (parts.next(), parts.next(), parts.next())
            else {
                continue;
            };
            let (Ok(id), Ok(done)) = (id.parse::<u32>(), done.parse::<bool>()) else {
                continue;
            };
            state.todos.push(Todo {
                id,
                done,
                text: unescape(text),
            });
            state.next_id = state.next_id.max(id + 1);
        }
        state
    }

    /// Builds the entire `<body>` inner HTML from scratch on every state change.
    /// No diffing: for a todo list at human-typing speed, rebuilding a few dozen
    /// `<li>`s is cheaper than the bookkeeping a virtual DOM would need, and it's
    /// the whole reason this project doesn't reach for a framework.
    pub fn render(&self) -> String {
        let items: String = self
            .visible()
            .map(|t| {
                format!(
                    "<li class=\"{done_class}\" data-id=\"{id}\">\
                       <input type=\"checkbox\" class=\"toggle\" data-id=\"{id}\"{checked}>\
                       <span class=\"text\">{text}</span>\
                       <button class=\"remove\" data-id=\"{id}\">×</button>\
                     </li>",
                    done_class = if t.done { "done" } else { "" },
                    id = t.id,
                    checked = if t.done { " checked" } else { "" },
                    text = escape_html(&t.text),
                )
            })
            .collect();

        format!(
            "<header>\
               <h1>todos</h1>\
               <input id=\"new-todo\" autofocus placeholder=\"What needs doing?\">\
             </header>\
             <ul id=\"list\">{items}</ul>\
             <footer>\
               <span id=\"count\">{remaining} item{plural} left</span>\
               <span class=\"filters\">\
                 <a href=\"#/\" class=\"{all_sel}\">All</a>\
                 <a href=\"#/active\" class=\"{active_sel}\">Active</a>\
                 <a href=\"#/completed\" class=\"{completed_sel}\">Completed</a>\
               </span>\
               <button id=\"clear-completed\">Clear completed</button>\
             </footer>",
            items = items,
            remaining = self.remaining_count(),
            plural = if self.remaining_count() == 1 { "" } else { "s" },
            all_sel = if self.filter == Filter::All {
                "selected"
            } else {
                ""
            },
            active_sel = if self.filter == Filter::Active {
                "selected"
            } else {
                ""
            },
            completed_sel = if self.filter == Filter::Completed {
                "selected"
            } else {
                ""
            },
        )
    }
}

fn escape(s: &str) -> String {
    s.replace('\\', "\\\\")
        .replace('\t', "\\t")
        .replace('\n', "\\n")
}

fn unescape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut chars = s.chars();
    while let Some(c) = chars.next() {
        if c == '\\' {
            match chars.next() {
                Some('t') => out.push('\t'),
                Some('n') => out.push('\n'),
                Some('\\') => out.push('\\'),
                Some(other) => {
                    out.push('\\');
                    out.push(other);
                }
                None => out.push('\\'),
            }
        } else {
            out.push(c);
        }
    }
    out
}

/// The list is rendered with `set_inner_html`, so todo text (user input) must be
/// escaped or a todo named `<img src=x onerror=alert(1)>` runs as script.
fn escape_html(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_appends_trimmed_todo_with_incrementing_id() {
        let mut s = AppState::new();
        assert!(s.add("  write tests  "));
        assert!(s.add("ship it"));
        assert_eq!(s.todos[0].text, "write tests");
        assert_eq!(s.todos[0].id, 1);
        assert_eq!(s.todos[1].id, 2);
    }

    #[test]
    fn add_rejects_blank_input() {
        let mut s = AppState::new();
        assert!(!s.add("   "));
        assert!(!s.add(""));
        assert!(s.todos.is_empty());
    }

    #[test]
    fn toggle_flips_done_and_ignores_unknown_id() {
        let mut s = AppState::new();
        s.add("a");
        s.toggle(1);
        assert!(s.todos[0].done);
        s.toggle(1);
        assert!(!s.todos[0].done);
        s.toggle(999); // no panic, no effect
        assert_eq!(s.todos.len(), 1);
    }

    #[test]
    fn remove_deletes_only_the_matching_id() {
        let mut s = AppState::new();
        s.add("a");
        s.add("b");
        s.remove(1);
        assert_eq!(s.todos.len(), 1);
        assert_eq!(s.todos[0].text, "b");
    }

    #[test]
    fn clear_completed_keeps_only_active() {
        let mut s = AppState::new();
        s.add("a");
        s.add("b");
        s.toggle(1);
        s.clear_completed();
        assert_eq!(s.todos.len(), 1);
        assert_eq!(s.todos[0].id, 2);
    }

    #[test]
    fn filter_narrows_visible_todos() {
        let mut s = AppState::new();
        s.add("a");
        s.add("b");
        s.toggle(1);

        s.set_filter(Filter::Active);
        assert_eq!(s.visible().count(), 1);
        assert_eq!(s.visible().next().unwrap().id, 2);

        s.set_filter(Filter::Completed);
        assert_eq!(s.visible().count(), 1);
        assert_eq!(s.visible().next().unwrap().id, 1);

        s.set_filter(Filter::All);
        assert_eq!(s.visible().count(), 2);
    }

    #[test]
    fn remaining_count_excludes_done() {
        let mut s = AppState::new();
        s.add("a");
        s.add("b");
        s.add("c");
        s.toggle(2);
        assert_eq!(s.remaining_count(), 2);
    }

    #[test]
    fn filter_from_hash_roundtrips_through_as_hash() {
        for f in [Filter::All, Filter::Active, Filter::Completed] {
            assert_eq!(Filter::from_hash(f.as_hash()), f);
        }
        assert_eq!(Filter::from_hash("#/nonsense"), Filter::All);
    }

    #[test]
    fn serialize_deserialize_roundtrips_including_tabs_and_newlines() {
        let mut s = AppState::new();
        s.add("buy\tmilk");
        s.add("multi\nline");
        s.toggle(1);

        let restored = AppState::deserialize(&s.serialize());
        assert_eq!(restored.todos, s.todos);
    }

    #[test]
    fn deserialize_skips_malformed_lines_without_panicking() {
        let s = AppState::deserialize("not-a-valid-row\n1\ttrue\tok\n\nfoo\tbar");
        assert_eq!(s.todos.len(), 1);
        assert_eq!(s.todos[0].text, "ok");
    }

    #[test]
    fn render_escapes_html_in_todo_text() {
        let mut s = AppState::new();
        s.add("<script>alert(1)</script>");
        let html = s.render();
        assert!(!html.contains("<script>"));
        assert!(html.contains("&lt;script&gt;"));
    }

    #[test]
    fn render_reflects_remaining_count_and_selected_filter() {
        let mut s = AppState::new();
        s.add("a");
        s.add("b");
        s.toggle(1);
        s.set_filter(Filter::Active);
        let html = s.render();
        assert!(html.contains("1 item left"));
        assert!(html.contains("class=\"selected\">Active"));
    }
}
