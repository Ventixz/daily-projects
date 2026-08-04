use std::io::{self, Write, stdout};

use crossterm::cursor::{Hide, MoveTo, Show};
use crossterm::event::{self, Event, KeyEvent};
use crossterm::style::{Print, ResetColor, SetBackgroundColor, SetForegroundColor};
use crossterm::terminal::{Clear, ClearType, disable_raw_mode, enable_raw_mode};
use crossterm::{execute, queue};

use crate::document::Position;

/// Thin wrapper around crossterm so the rest of the editor talks in
/// `Position`s and plain method calls rather than escape sequences —
/// this is the only module that imports crossterm directly.
pub struct Terminal {
    size: (u16, u16),
}

impl Terminal {
    pub fn new() -> io::Result<Self> {
        let (cols, rows) = crossterm::terminal::size()?;
        enable_raw_mode()?;
        Ok(Self {
            // Reserve the bottom two rows for the status bar and the
            // message line, same split the hecto tutorial uses.
            size: (cols, rows.saturating_sub(2)),
        })
    }

    pub fn size(&self) -> (u16, u16) {
        self.size
    }

    pub fn clear_screen() {
        let _ = execute!(stdout(), Clear(ClearType::All));
    }

    pub fn clear_current_line() {
        let _ = queue!(stdout(), Clear(ClearType::CurrentLine));
    }

    pub fn cursor_position(pos: &Position) {
        let x = pos.x.min(u16::MAX as usize) as u16;
        let y = pos.y.min(u16::MAX as usize) as u16;
        let _ = queue!(stdout(), MoveTo(x, y));
    }

    pub fn flush() -> io::Result<()> {
        stdout().flush()
    }

    pub fn cursor_hide() {
        let _ = queue!(stdout(), Hide);
    }

    pub fn cursor_show() {
        let _ = queue!(stdout(), Show);
    }

    pub fn queue_print(text: &str) {
        let _ = queue!(stdout(), Print(text));
    }

    pub fn set_bg_color(color: crossterm::style::Color) {
        let _ = queue!(stdout(), SetBackgroundColor(color));
    }

    pub fn set_fg_color(color: crossterm::style::Color) {
        let _ = queue!(stdout(), SetForegroundColor(color));
    }

    pub fn reset_color() {
        let _ = queue!(stdout(), ResetColor);
    }

    pub fn read_key() -> io::Result<KeyEvent> {
        loop {
            if let Event::Key(key_event) = event::read()? {
                return Ok(key_event);
            }
        }
    }
}

impl Drop for Terminal {
    fn drop(&mut self) {
        let _ = disable_raw_mode();
    }
}
