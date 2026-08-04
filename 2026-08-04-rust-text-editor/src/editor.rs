use std::io;

use crossterm::event::{KeyCode, KeyEvent, KeyModifiers};
use crossterm::style::Color;

use crate::document::{Document, Position};
use crate::terminal::Terminal;

const QUIT_TIMES: u8 = 3;
const STATUS_BG: Color = Color::Rgb {
    r: 63,
    g: 63,
    b: 63,
};
const STATUS_FG: Color = Color::Rgb {
    r: 239,
    g: 239,
    b: 239,
};

/// Ties `Document` (what's being edited) and `Terminal` (how it's drawn)
/// together with the cursor/scroll/status state that only makes sense
/// while a session is running.
pub struct Editor {
    should_quit: bool,
    terminal: Terminal,
    cursor_position: Position,
    offset: Position,
    document: Document,
    status_message: String,
    quit_times: u8,
}

impl Editor {
    pub fn new(filename: Option<&str>) -> io::Result<Self> {
        let (document, status_message) = match filename {
            Some(name) => match Document::open(name) {
                Ok(doc) => (doc, String::new()),
                Err(_) => (
                    Document::default(),
                    format!("ERR: Could not open file: {name}"),
                ),
            },
            None => (
                Document::default(),
                "HELP: Ctrl-S = save | Ctrl-F = find | Ctrl-Q = quit".to_string(),
            ),
        };
        Ok(Self {
            should_quit: false,
            terminal: Terminal::new()?,
            cursor_position: Position { x: 0, y: 0 },
            offset: Position { x: 0, y: 0 },
            document,
            status_message,
            quit_times: QUIT_TIMES,
        })
    }

    pub fn run(&mut self) {
        loop {
            if let Err(error) = self.refresh_screen() {
                die(&error);
            }
            if self.should_quit {
                break;
            }
            if let Err(error) = self.process_keypress() {
                die(&error);
            }
        }
    }

    fn refresh_screen(&mut self) -> io::Result<()> {
        Terminal::cursor_hide();
        Terminal::cursor_position(&Position { x: 0, y: 0 });
        if self.should_quit {
            Terminal::clear_screen();
            Terminal::queue_print("Goodbye.\r\n");
        } else {
            self.draw_rows();
            self.draw_status_bar();
            self.draw_message_bar();
            Terminal::cursor_position(&Position {
                x: self.cursor_position.x.saturating_sub(self.offset.x),
                y: self.cursor_position.y.saturating_sub(self.offset.y),
            });
        }
        Terminal::cursor_show();
        Terminal::flush()
    }

    fn process_keypress(&mut self) -> io::Result<()> {
        let pressed_key = Terminal::read_key()?;
        match (pressed_key.code, pressed_key.modifiers) {
            (KeyCode::Char('q'), KeyModifiers::CONTROL) => {
                if self.document.is_dirty() && self.quit_times > 0 {
                    self.status_message = format!(
                        "WARNING! File has unsaved changes. Press Ctrl-Q {} more times to quit.",
                        self.quit_times
                    );
                    self.quit_times -= 1;
                    return Ok(());
                }
                self.should_quit = true;
            }
            (KeyCode::Char('s'), KeyModifiers::CONTROL) => self.save(),
            (KeyCode::Char('f'), KeyModifiers::CONTROL) => self.search(),
            (KeyCode::Char(c), _) => {
                self.document.insert(&self.cursor_position, c);
                self.move_cursor(KeyCode::Right);
            }
            (KeyCode::Enter, _) => {
                self.document.insert(&self.cursor_position, '\n');
                self.move_cursor(KeyCode::Right);
            }
            (KeyCode::Delete, _) => self.document.delete(&self.cursor_position),
            (KeyCode::Backspace, _) => {
                if self.cursor_position.x > 0 || self.cursor_position.y > 0 {
                    self.move_cursor(KeyCode::Left);
                    self.document.delete(&self.cursor_position);
                }
            }
            (
                KeyCode::Up
                | KeyCode::Down
                | KeyCode::Left
                | KeyCode::Right
                | KeyCode::PageUp
                | KeyCode::PageDown
                | KeyCode::Home
                | KeyCode::End,
                _,
            ) => self.move_cursor(pressed_key.code),
            _ => (),
        }
        self.scroll();
        if self.quit_times < QUIT_TIMES {
            self.quit_times = QUIT_TIMES;
            self.status_message.clear();
        }
        Ok(())
    }

    fn save(&mut self) {
        if self.document.file_name.is_none() {
            let new_name = self.prompt("Save as: ", |_, _, _| {}).unwrap_or(None);
            if new_name.is_none() {
                self.status_message = "Save aborted.".to_string();
                return;
            }
            self.document.file_name = new_name;
        }
        self.status_message = if self.document.save().is_ok() {
            "File saved successfully.".to_string()
        } else {
            "Error writing file!".to_string()
        };
    }

    fn search(&mut self) {
        let old_position = self.cursor_position;
        let query = self
            .prompt("Search (ESC to cancel): ", |editor, key, query| {
                if key.code != KeyCode::Enter
                    && let Some(position) = editor.document.find(query, &editor.cursor_position)
                {
                    editor.cursor_position = position;
                    editor.scroll();
                }
            })
            .unwrap_or(None);
        if query.is_none() {
            self.cursor_position = old_position;
            self.scroll();
        }
    }

    /// Reads a line at the bottom message bar, calling `callback` after
    /// every keystroke so incremental search can jump the cursor live.
    /// Returns `Ok(None)` on Escape, `Ok(Some(""))` on empty confirm.
    fn prompt(
        &mut self,
        prompt: &str,
        mut callback: impl FnMut(&mut Self, KeyEvent, &str),
    ) -> io::Result<Option<String>> {
        let mut result = String::new();
        loop {
            self.status_message = format!("{prompt}{result}");
            self.refresh_screen()?;
            let key = Terminal::read_key()?;
            match key.code {
                KeyCode::Backspace => {
                    result.pop();
                }
                KeyCode::Enter => break,
                KeyCode::Esc => {
                    result.clear();
                    callback(self, key, &result);
                    return Ok(None);
                }
                KeyCode::Char(c)
                    if key.modifiers.is_empty() || key.modifiers == KeyModifiers::SHIFT =>
                {
                    result.push(c);
                }
                _ => (),
            }
            callback(self, key, &result);
        }
        self.status_message.clear();
        Ok(Some(result))
    }

    fn move_cursor(&mut self, key: KeyCode) {
        let Position { mut x, mut y } = self.cursor_position;
        let height = self.document.len();
        let mut width = self.document.row(y).map_or(0, |row| row.len());

        match key {
            KeyCode::Up => y = y.saturating_sub(1),
            KeyCode::Down if y < height => y = y.saturating_add(1),
            KeyCode::Left => {
                if x > 0 {
                    x -= 1;
                } else if y > 0 {
                    y -= 1;
                    x = self.document.row(y).map_or(0, |row| row.len());
                }
            }
            KeyCode::Right => {
                if x < width {
                    x += 1;
                } else if y < height {
                    y += 1;
                    x = 0;
                }
            }
            KeyCode::PageUp => y = y.saturating_sub(self.terminal.size().1 as usize),
            KeyCode::PageDown => {
                y = (y + self.terminal.size().1 as usize).min(height);
            }
            KeyCode::Home => x = 0,
            KeyCode::End => x = width,
            _ => (),
        }
        width = self.document.row(y).map_or(0, |row| row.len());
        if x > width {
            x = width;
        }
        self.cursor_position = Position { x, y };
    }

    fn scroll(&mut self) {
        let Position { x, y } = self.cursor_position;
        let (width, height) = self.terminal.size();
        let (width, height) = (width as usize, height as usize);

        if y < self.offset.y {
            self.offset.y = y;
        } else if y >= self.offset.y.saturating_add(height) {
            self.offset.y = y.saturating_sub(height).saturating_add(1);
        }
        if x < self.offset.x {
            self.offset.x = x;
        } else if x >= self.offset.x.saturating_add(width) {
            self.offset.x = x.saturating_sub(width).saturating_add(1);
        }
    }

    fn draw_rows(&self) {
        let height = self.terminal.size().1;
        for terminal_row in 0..height {
            Terminal::clear_current_line();
            let file_row = terminal_row as usize + self.offset.y;
            if let Some(row) = self.document.row(file_row) {
                self.draw_row(row);
            } else if self.document.is_empty() && terminal_row == height / 3 {
                self.draw_welcome_message();
            } else {
                Terminal::queue_print("~");
            }
            Terminal::queue_print("\r\n");
        }
    }

    fn draw_row(&self, row: &crate::row::Row) {
        let width = self.terminal.size().0 as usize;
        let start = self.offset.x;
        let end = self.offset.x + width;
        Terminal::queue_print(&row.render_slice(start, end));
    }

    fn draw_welcome_message(&self) {
        let mut welcome_message = format!("Hecto editor -- version {}", env!("CARGO_PKG_VERSION"));
        let width = self.terminal.size().0 as usize;
        let len = welcome_message.len();
        let padding = width.saturating_sub(len) / 2;
        let spaces = " ".repeat(padding.saturating_sub(1));
        welcome_message = format!("~{spaces}{welcome_message}");
        welcome_message.truncate(width);
        Terminal::queue_print(&welcome_message);
    }

    fn draw_status_bar(&self) {
        let width = self.terminal.size().0 as usize;
        let modified = if self.document.is_dirty() {
            " (modified)"
        } else {
            ""
        };
        let file_name = self
            .document
            .file_name
            .clone()
            .unwrap_or_else(|| "[No Name]".to_string());
        let mut status = format!(
            "{:.20}{modified} - {} lines",
            file_name,
            self.document.len()
        );
        let line_indicator = format!(
            "{}/{}",
            self.cursor_position.y.saturating_add(1),
            self.document.len()
        );
        let len = status.len() + line_indicator.len();
        status.push_str(&" ".repeat(width.saturating_sub(len)));
        status = format!("{status}{line_indicator}");
        status.truncate(width);

        Terminal::set_bg_color(STATUS_BG);
        Terminal::set_fg_color(STATUS_FG);
        Terminal::queue_print(&status);
        Terminal::reset_color();
        Terminal::queue_print("\r\n");
    }

    fn draw_message_bar(&self) {
        Terminal::clear_current_line();
        let mut text = self.status_message.clone();
        text.truncate(self.terminal.size().0 as usize);
        Terminal::queue_print(&text);
    }
}

fn die(e: &io::Error) {
    Terminal::clear_screen();
    panic!("{e}");
}
