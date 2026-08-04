use std::fs;
use std::io::{self, Write};

use crate::row::Row;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Position {
    pub x: usize,
    pub y: usize,
}

/// The in-memory buffer: an ordered list of `Row`s plus the bookkeeping
/// (filename, dirty flag) that `save()`/`Ctrl-S` care about.
#[derive(Debug, Default)]
pub struct Document {
    rows: Vec<Row>,
    pub file_name: Option<String>,
    dirty: bool,
}

impl Document {
    pub fn open(filename: &str) -> io::Result<Self> {
        let contents = fs::read_to_string(filename)?;
        let rows = contents.lines().map(Row::from).collect();
        Ok(Self {
            rows,
            file_name: Some(filename.to_string()),
            dirty: false,
        })
    }

    pub fn save(&mut self) -> io::Result<()> {
        let Some(name) = &self.file_name else {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "no file name set",
            ));
        };
        let mut file = fs::File::create(name)?;
        for row in &self.rows {
            file.write_all(row.as_string().as_bytes())?;
            file.write_all(b"\n")?;
        }
        self.dirty = false;
        Ok(())
    }

    pub fn is_dirty(&self) -> bool {
        self.dirty
    }

    pub fn len(&self) -> usize {
        self.rows.len()
    }

    pub fn is_empty(&self) -> bool {
        self.rows.is_empty()
    }

    pub fn row(&self, index: usize) -> Option<&Row> {
        self.rows.get(index)
    }

    /// Inserts `c` at `at`, splitting a new row when `at.x` lands past the
    /// end of the row (i.e. pressing Enter) versus splicing mid-line.
    pub fn insert(&mut self, at: &Position, c: char) {
        if at.y > self.rows.len() {
            return;
        }
        self.dirty = true;
        if c == '\n' {
            self.insert_newline(at);
            return;
        }
        if at.y == self.rows.len() {
            let mut row = Row::default();
            row.insert(0, c);
            self.rows.push(row);
        } else {
            let row = &mut self.rows[at.y];
            row.insert(at.x, c);
        }
    }

    fn insert_newline(&mut self, at: &Position) {
        if at.y >= self.rows.len() {
            self.rows.push(Row::default());
            return;
        }
        let new_row = self.rows[at.y].split(at.x);
        self.rows.insert(at.y + 1, new_row);
    }

    /// Deletes the character at `at`. At end-of-line this joins the next
    /// row up instead (mirroring what Delete does in any editor); at the
    /// very end of the document it's a no-op.
    pub fn delete(&mut self, at: &Position) {
        if at.y >= self.rows.len() {
            return;
        }
        self.dirty = true;
        if at.x == self.rows[at.y].len() && at.y + 1 < self.rows.len() {
            let next_row = self.rows.remove(at.y + 1);
            self.rows[at.y].append(&next_row);
        } else {
            self.rows[at.y].delete(at.x);
        }
    }

    /// Searches forward from `from` (inclusive of its row) for `query`,
    /// wrapping to the top of the document if nothing matches below it —
    /// standard incremental-search behavior.
    pub fn find(&self, query: &str, from: &Position) -> Option<Position> {
        if query.is_empty() {
            return None;
        }
        for y in from.y..self.rows.len() {
            let start_x = if y == from.y { from.x } else { 0 };
            if let Some(x) = self.rows[y].find(query, start_x) {
                return Some(Position { x, y });
            }
        }
        for y in 0..from.y.min(self.rows.len()) {
            if let Some(x) = self.rows[y].find(query, 0) {
                return Some(Position { x, y });
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Read;

    fn temp_path(name: &str) -> std::path::PathBuf {
        let mut p = std::env::temp_dir();
        p.push(format!("hecto-test-{}-{}", std::process::id(), name));
        p
    }

    #[test]
    fn open_reads_file_into_rows() {
        let path = temp_path("open.txt");
        fs::write(&path, "line one\nline two\n").unwrap();

        let doc = Document::open(path.to_str().unwrap()).unwrap();

        assert_eq!(doc.len(), 2);
        assert_eq!(doc.row(0).unwrap().as_string(), "line one");
        assert_eq!(doc.row(1).unwrap().as_string(), "line two");
        assert!(!doc.is_dirty());

        fs::remove_file(&path).ok();
    }

    #[test]
    fn save_round_trips_edits_to_disk() {
        let path = temp_path("save.txt");
        fs::write(&path, "hello\n").unwrap();

        let mut doc = Document::open(path.to_str().unwrap()).unwrap();
        doc.insert(&Position { x: 5, y: 0 }, '!');
        assert!(doc.is_dirty());
        doc.save().unwrap();
        assert!(!doc.is_dirty());

        let mut saved = String::new();
        fs::File::open(&path)
            .unwrap()
            .read_to_string(&mut saved)
            .unwrap();
        assert_eq!(saved, "hello!\n");

        fs::remove_file(&path).ok();
    }

    #[test]
    fn insert_newline_splits_the_row_in_two() {
        let mut doc = Document::default();
        doc.insert(&Position { x: 0, y: 0 }, 'a');
        doc.insert(&Position { x: 1, y: 0 }, 'b');
        doc.insert(&Position { x: 2, y: 0 }, 'c');
        doc.insert(&Position { x: 1, y: 0 }, '\n');

        assert_eq!(doc.len(), 2);
        assert_eq!(doc.row(0).unwrap().as_string(), "a");
        assert_eq!(doc.row(1).unwrap().as_string(), "bc");
    }

    #[test]
    fn delete_at_end_of_line_joins_the_next_row() {
        let mut doc = Document::default();
        doc.insert(&Position { x: 0, y: 0 }, 'a');
        doc.insert(&Position { x: 0, y: 1 }, 'b');
        assert_eq!(doc.len(), 2);

        doc.delete(&Position { x: 1, y: 0 });

        assert_eq!(doc.len(), 1);
        assert_eq!(doc.row(0).unwrap().as_string(), "ab");
    }

    #[test]
    fn delete_at_end_of_document_is_a_noop() {
        let mut doc = Document::default();
        doc.insert(&Position { x: 0, y: 0 }, 'a');
        doc.delete(&Position { x: 1, y: 0 });
        assert_eq!(doc.row(0).unwrap().as_string(), "a");
    }

    #[test]
    fn find_wraps_around_to_the_top() {
        let doc_str = "apple\nbanana\ncherry\n";
        let path = temp_path("find.txt");
        fs::write(&path, doc_str).unwrap();
        let doc = Document::open(path.to_str().unwrap()).unwrap();

        // Search starting *after* "apple" should wrap around and find it
        // again, rather than reporting no match.
        let found = doc.find("apple", &Position { x: 0, y: 1 });
        assert_eq!(found, Some(Position { x: 0, y: 0 }));

        fs::remove_file(&path).ok();
    }

    #[test]
    fn find_returns_none_when_query_is_absent() {
        let mut doc = Document::default();
        doc.insert(&Position { x: 0, y: 0 }, 'a');
        assert_eq!(doc.find("zzz", &Position { x: 0, y: 0 }), None);
    }
}
