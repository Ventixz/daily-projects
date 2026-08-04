const TAB_STOP: usize = 8;

/// One line of a `Document`, stored as raw characters plus a cached,
/// tab-expanded render string so the terminal never has to redo that work
/// on every keystroke.
#[derive(Debug, Default, Clone)]
pub struct Row {
    chars: Vec<char>,
    render: String,
}

impl Row {
    pub fn len(&self) -> usize {
        self.chars.len()
    }

    pub fn is_empty(&self) -> bool {
        self.chars.is_empty()
    }

    /// The tab-expanded string that should actually be drawn to the screen.
    pub fn render(&self) -> &str {
        &self.render
    }

    /// Render, but sliced by *character* index, not byte index — needed
    /// because the render string's tab expansion means char and byte
    /// offsets diverge as soon as a line has a tab in it.
    pub fn render_slice(&self, start: usize, end: usize) -> String {
        let chars: Vec<char> = self.render.chars().collect();
        let end = end.min(chars.len());
        let start = start.min(end);
        chars[start..end].iter().collect()
    }

    pub fn insert(&mut self, at: usize, c: char) {
        let at = at.min(self.chars.len());
        self.chars.insert(at, c);
        self.rebuild_render();
    }

    /// Removes the character at `at`. No-op if `at` is out of range, so
    /// callers don't need a bounds check before every delete.
    pub fn delete(&mut self, at: usize) {
        if at >= self.chars.len() {
            return;
        }
        self.chars.remove(at);
        self.rebuild_render();
    }

    /// Splits this row at `at`, keeping `[0, at)` here and returning a new
    /// `Row` holding `[at, len)` — the second half after pressing Enter.
    pub fn split(&mut self, at: usize) -> Row {
        let at = at.min(self.chars.len());
        let tail: Vec<char> = self.chars.split_off(at);
        self.rebuild_render();
        Row::from(tail.into_iter().collect::<String>().as_str())
    }

    /// Appends another row's content to the end of this one — joining the
    /// next line up when Backspace is pressed at column 0.
    pub fn append(&mut self, other: &Row) {
        self.chars.extend(other.chars.iter());
        self.rebuild_render();
    }

    /// Finds the first occurrence of `query` at or after character index
    /// `from`, returning its character index. Case-insensitive, matching
    /// how most terminal editors' incremental search behaves.
    pub fn find(&self, query: &str, from: usize) -> Option<usize> {
        if query.is_empty() || from > self.chars.len() {
            return None;
        }
        let haystack: String = self.chars[from..].iter().collect();
        let haystack_lower = haystack.to_lowercase();
        let query_lower = query.to_lowercase();
        haystack_lower
            .find(&query_lower)
            // The match index from `find` is a *byte* offset into the
            // lowercased haystack; converting back to a char offset keeps
            // this correct for any non-ASCII content.
            .map(|byte_idx| from + haystack_lower[..byte_idx].chars().count())
    }

    fn rebuild_render(&mut self) {
        let mut render = String::with_capacity(self.chars.len());
        let mut col = 0;
        for &c in &self.chars {
            if c == '\t' {
                render.push(' ');
                col += 1;
                while col % TAB_STOP != 0 {
                    render.push(' ');
                    col += 1;
                }
            } else {
                render.push(c);
                col += 1;
            }
        }
        self.render = render;
    }

    pub fn as_string(&self) -> String {
        self.chars.iter().collect()
    }
}

impl From<&str> for Row {
    fn from(s: &str) -> Self {
        let mut row = Row {
            chars: s.chars().collect(),
            render: String::new(),
        };
        row.rebuild_render();
        row
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn insert_and_delete_roundtrip() {
        let mut row = Row::from("helo");
        row.insert(3, 'l');
        assert_eq!(row.as_string(), "hello");
        row.delete(0);
        assert_eq!(row.as_string(), "ello");
    }

    #[test]
    fn delete_out_of_range_is_a_noop() {
        let mut row = Row::from("hi");
        row.delete(50);
        assert_eq!(row.as_string(), "hi");
    }

    #[test]
    fn split_keeps_head_and_returns_tail() {
        let mut row = Row::from("hello world");
        let tail = row.split(5);
        assert_eq!(row.as_string(), "hello");
        assert_eq!(tail.as_string(), " world");
    }

    #[test]
    fn append_joins_two_rows() {
        let mut row = Row::from("hello");
        let other = Row::from(" world");
        row.append(&other);
        assert_eq!(row.as_string(), "hello world");
    }

    #[test]
    fn tabs_expand_to_the_next_stop() {
        let row = Row::from("a\tb");
        // 'a' at col 0, tab pads to col 8, 'b' at col 8.
        assert_eq!(row.render(), "a       b");
    }

    #[test]
    fn find_is_case_insensitive_and_returns_char_index() {
        let row = Row::from("Hello, World");
        assert_eq!(row.find("world", 0), Some(7));
        assert_eq!(row.find("xyz", 0), None);
    }

    #[test]
    fn find_respects_the_from_offset() {
        let row = Row::from("ababab");
        assert_eq!(row.find("ab", 0), Some(0));
        assert_eq!(row.find("ab", 1), Some(2));
        assert_eq!(row.find("ab", 3), Some(4));
    }

    #[test]
    fn find_char_offset_survives_multibyte_content() {
        // "héllo" — é is 2 bytes in UTF-8 but 1 char, so a byte-index bug
        // would put "world" one column too far right.
        let row = Row::from("héllo world");
        assert_eq!(row.find("world", 0), Some(6));
    }
}
