//! Draws a [`Game`] into a [`Buffer`]. Pure function of the game state:
//! rendering never changes the game, so it can run at any frame rate.

use crate::art::art;
use crate::buffer::{rgb, Buffer};
use crate::font::{draw_centered, draw_text, text_width};
use crate::game::*;

pub const BACKGROUND: u32 = rgb(8, 8, 20);
pub const TEXT: u32 = rgb(235, 235, 235);
pub const ACCENT: u32 = rgb(255, 200, 60);
pub const PLAYER: u32 = rgb(80, 230, 110);
pub const SHIELD: u32 = rgb(80, 230, 110);
pub const SAUCER: u32 = rgb(240, 70, 70);
pub const ENEMY_SHOT: u32 = rgb(255, 255, 255);
pub const PLAYER_SHOT: u32 = rgb(255, 255, 160);

pub fn alien_color(kind: AlienKind) -> u32 {
    match kind {
        AlienKind::Drone => rgb(230, 120, 245),
        AlienKind::Stinger => rgb(100, 210, 245),
        AlienKind::Brute => rgb(245, 225, 100),
    }
}

pub fn new_buffer() -> Buffer {
    Buffer::new(WIDTH as usize, HEIGHT as usize)
}

pub fn render(g: &Game, buf: &mut Buffer) {
    buf.clear(BACKGROUND);
    draw_header(g, buf);
    match g.phase {
        Phase::Title => draw_title(buf, g.tick),
        _ => draw_world(g, buf),
    }
    match g.phase {
        Phase::WaveClear(_) => {
            draw_centered(buf, &format!("WAVE {} CLEAR", g.wave), 136, ACCENT);
        }
        Phase::GameOver(t) => {
            buf.fill_rect(40, 128, WIDTH - 80, 36, BACKGROUND);
            draw_centered(buf, "GAME OVER", 134, SAUCER);
            if t == 0 && (g.tick / 30).is_multiple_of(2) {
                draw_centered(buf, "PRESS FIRE", 150, TEXT);
            }
        }
        _ => {}
    }
}

fn draw_header(g: &Game, buf: &mut Buffer) {
    draw_text(buf, &format!("SCORE {:05}", g.score), 8, 4, TEXT);
    let hi = format!("HI {:05}", g.hi_score);
    draw_text(buf, &hi, WIDTH - 8 - text_width(&hi), 4, TEXT);
}

fn draw_title(buf: &mut Buffer, tick: u64) {
    let a = art();
    draw_centered(buf, "STARFALL", 40, ACCENT);
    draw_centered(buf, "INVADERS FROM SCRATCH", 54, TEXT);
    draw_centered(buf, "- SCORE TABLE -", 84, TEXT);
    let rows: [(&crate::sprite::Sprite, u32, &str); 4] = [
        (&a.saucer, SAUCER, "= ? MYSTERY"),
        (&a.drone[0], alien_color(AlienKind::Drone), "= 30 POINTS"),
        (
            &a.stinger[0],
            alien_color(AlienKind::Stinger),
            "= 20 POINTS",
        ),
        (&a.brute[0], alien_color(AlienKind::Brute), "= 10 POINTS"),
    ];
    for (i, (sprite, color, label)) in rows.iter().enumerate() {
        let y = 102 + i as i32 * 16;
        buf.draw_sprite(sprite, 76 - sprite.w as i32 / 2, y, *color);
        draw_text(buf, label, 92, y, TEXT);
    }
    if (tick / 30).is_multiple_of(2) {
        draw_centered(buf, "PRESS FIRE TO START", 182, ACCENT);
    }
    draw_centered(buf, "ARROWS MOVE  SPACE FIRES", 206, TEXT);
    draw_centered(buf, "P PAUSES  ESC QUITS", 218, TEXT);
}

fn draw_world(g: &Game, buf: &mut Buffer) {
    let a = art();
    for sh in &g.shields {
        buf.draw_sprite(&sh.bitmap, sh.x, sh.y, SHIELD);
    }
    for al in &g.aliens {
        let (x, y) = g.alien_pos(al);
        if al.alive {
            buf.draw_sprite(g.alien_sprite(al), x, y, alien_color(al.kind));
        } else if al.dying > 0 {
            let w = a.alien_death.w as i32 - al.kind.frames()[0].w as i32;
            buf.draw_sprite(&a.alien_death, x - w / 2, y, alien_color(al.kind));
        }
    }
    if let Some(s) = &g.saucer {
        if s.dying == 0 {
            buf.draw_sprite(&a.saucer, s.x, SAUCER_Y, SAUCER);
        } else if s.dying > 20 {
            buf.draw_sprite(&a.saucer_death, s.x, SAUCER_Y, SAUCER);
        } else {
            let label = s.points.to_string();
            draw_text(
                buf,
                &label,
                s.x + 8 - text_width(&label) / 2,
                SAUCER_Y,
                SAUCER,
            );
        }
    }
    if let Some(b) = g.player_bullet {
        buf.draw_sprite(&a.player_bullet, b.x, b.y, PLAYER_SHOT);
    }
    let shot = g.alien_bullet_frame();
    for b in &g.alien_bullets {
        buf.draw_sprite(shot, b.x, b.y, ENEMY_SHOT);
    }
    match g.phase {
        Phase::PlayerDying(_) => {
            let frame = &a.player_death[(g.tick / 6 % 2) as usize];
            buf.draw_sprite(frame, g.player_x, PLAYER_Y, PLAYER);
        }
        Phase::GameOver(_) => {}
        _ => buf.draw_sprite(&a.player, g.player_x, PLAYER_Y, PLAYER),
    }

    // Ground line and the footer: lives as a number plus spare ships.
    buf.fill_rect(0, GROUND_Y, WIDTH, 1, PLAYER);
    draw_text(buf, &g.lives.to_string(), 8, GROUND_Y + 6, TEXT);
    for i in 0..g.lives.saturating_sub(1).min(5) {
        buf.draw_sprite(&a.player, 20 + i as i32 * 16, GROUND_Y + 5, PLAYER);
    }
    let wave = format!("WAVE {}", g.wave);
    draw_text(
        buf,
        &wave,
        WIDTH - 8 - text_width(&wave),
        GROUND_Y + 6,
        TEXT,
    );
}
