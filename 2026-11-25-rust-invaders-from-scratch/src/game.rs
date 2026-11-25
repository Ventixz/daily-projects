//! The whole game as a deterministic state machine.
//!
//! [`Game::update`] advances exactly one fixed tick (1/60 s) given the keys
//! held during that tick, and returns the [`Event`]s that happened. There is
//! no clock, no I/O and no hidden randomness inside: the same seed and the
//! same inputs always produce the same game. The window frontend, the
//! headless demo and the tests all drive it the same way.
//!
//! Coordinates are pixels on a 224x256 screen, origin top-left, y down.

use crate::art::art;
use crate::rng::Rng;
use crate::sprite::{overlap, overlap_at, Rect, Sprite};

pub const WIDTH: i32 = 224;
pub const HEIGHT: i32 = 256;
pub const TICKS_PER_SECOND: u32 = 60;

pub const COLS: usize = 11;
pub const ROWS: usize = 5;
pub const CELL_W: i32 = 16;
pub const CELL_H: i32 = 16;

/// Top of the play field; the score bar lives above it.
pub const FIELD_TOP: i32 = 16;
pub const SAUCER_Y: i32 = 26;
pub const FORMATION_START_X: i32 = 24;
pub const FORMATION_START_Y: i32 = 48;
pub const SHIELD_Y: i32 = 188;
pub const SHIELD_XS: [i32; 4] = [32, 80, 128, 176];
pub const PLAYER_Y: i32 = 216;
pub const GROUND_Y: i32 = 236;
/// Horizontal no-go strip at both screen edges.
pub const MARGIN: i32 = 8;

pub const START_LIVES: u32 = 3;
pub const EXTRA_LIFE_SCORE: u32 = 1500;

const PLAYER_SPEED: i32 = 1;
const PLAYER_BULLET_SPEED: i32 = 4;
const ALIEN_BULLET_SPEED: i32 = 2;
const MAX_ALIEN_BULLETS: usize = 3;
const MARCH_DX: i32 = 2;
const MARCH_DY: i32 = 8;
const ALIEN_DEATH_TICKS: u8 = 16;
const SAUCER_DEATH_TICKS: u8 = 40;
const PLAYER_DEATH_TICKS: u32 = 90;
const WAVE_CLEAR_TICKS: u32 = 120;
const GAME_OVER_LOCKOUT_TICKS: u32 = 90;
const SAUCER_POINTS: [u32; 4] = [50, 100, 150, 300];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AlienKind {
    Drone,
    Stinger,
    Brute,
}

impl AlienKind {
    pub fn for_row(row: usize) -> AlienKind {
        match row {
            0 => AlienKind::Drone,
            1 | 2 => AlienKind::Stinger,
            _ => AlienKind::Brute,
        }
    }

    pub fn points(self) -> u32 {
        match self {
            AlienKind::Drone => 30,
            AlienKind::Stinger => 20,
            AlienKind::Brute => 10,
        }
    }

    pub fn frames(self) -> &'static [Sprite; 2] {
        let a = art();
        match self {
            AlienKind::Drone => &a.drone,
            AlienKind::Stinger => &a.stinger,
            AlienKind::Brute => &a.brute,
        }
    }
}

#[derive(Clone, Debug)]
pub struct Alien {
    pub kind: AlienKind,
    pub row: usize,
    pub col: usize,
    pub alive: bool,
    /// Ticks of explosion left to draw after being shot.
    pub dying: u8,
}

/// The block of aliens moves as one: an offset, a direction, and a timer
/// that fires one 2-pixel step every [`Game::march_interval`] ticks.
#[derive(Clone, Debug)]
pub struct Formation {
    pub x: i32,
    pub y: i32,
    /// +1 marching right, -1 marching left.
    pub dir: i32,
    /// Animation frame (0 or 1); flips on every step.
    pub frame: usize,
    pub timer: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Bullet {
    pub x: i32,
    pub y: i32,
}

#[derive(Clone, Debug)]
pub struct Saucer {
    pub x: i32,
    pub dir: i32,
    pub points: u32,
    /// Ticks of explosion left; 0 while flying.
    pub dying: u8,
}

#[derive(Clone, Debug)]
pub struct Shield {
    pub x: i32,
    pub y: i32,
    /// Starts as a copy of the shield art and is eroded pixel by pixel.
    pub bitmap: Sprite,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    Title,
    Playing,
    /// The player was hit; the world freezes while the wreck animates.
    PlayerDying(u32),
    /// Every alien is down; a pause before the next wave.
    WaveClear(u32),
    /// Fire is ignored until the counter runs out, so a held key can't skip it.
    GameOver(u32),
}

/// The keys held during one tick. Fire is a level; the game turns it into a
/// press itself, so holding the key doesn't autofire.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Input {
    pub left: bool,
    pub right: bool,
    pub fire: bool,
    pub start: bool,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Event {
    GameStarted,
    Shot,
    AlienKilled { kind: AlienKind, points: u32 },
    SaucerKilled { points: u32 },
    BulletsCollided,
    ShieldHit,
    PlayerHit,
    Invaded,
    ExtraLife,
    WaveCleared(u32),
    WaveStarted(u32),
    GameOver { score: u32 },
}

pub struct Game {
    pub phase: Phase,
    pub score: u32,
    pub hi_score: u32,
    pub lives: u32,
    pub wave: u32,
    /// Ticks since the game object was created; drives animations.
    pub tick: u64,
    pub player_x: i32,
    pub aliens: Vec<Alien>,
    pub formation: Formation,
    pub player_bullet: Option<Bullet>,
    pub alien_bullets: Vec<Bullet>,
    pub shields: Vec<Shield>,
    pub saucer: Option<Saucer>,
    rng: Rng,
    prev_fire: bool,
    alien_fire_timer: u32,
    saucer_timer: u32,
    extra_life_given: bool,
}

impl Game {
    pub fn new(seed: u64) -> Game {
        let mut g = Game {
            phase: Phase::Title,
            score: 0,
            hi_score: 0,
            lives: START_LIVES,
            wave: 1,
            tick: 0,
            player_x: 0,
            aliens: Vec::new(),
            formation: Formation {
                x: 0,
                y: 0,
                dir: 1,
                frame: 0,
                timer: 0,
            },
            player_bullet: None,
            alien_bullets: Vec::new(),
            shields: Vec::new(),
            saucer: None,
            rng: Rng::new(seed),
            prev_fire: false,
            alien_fire_timer: 0,
            saucer_timer: 0,
            extra_life_given: false,
        };
        g.setup_wave();
        g
    }

    // ---- geometry ---------------------------------------------------------

    pub fn player_w() -> i32 {
        art().player.w as i32
    }

    /// Top-left of an alien's sprite. Sprites of different widths are
    /// centred in their 16-pixel cell so the columns line up.
    pub fn alien_pos(&self, a: &Alien) -> (i32, i32) {
        let w = a.kind.frames()[0].w as i32;
        (
            self.formation.x + a.col as i32 * CELL_W + (CELL_W - w) / 2,
            self.formation.y + a.row as i32 * CELL_H,
        )
    }

    pub fn alien_sprite(&self, a: &Alien) -> &'static Sprite {
        &a.kind.frames()[self.formation.frame]
    }

    pub fn alive_count(&self) -> usize {
        self.aliens.iter().filter(|a| a.alive).count()
    }

    /// Ticks between formation steps. Fewer aliens means less work for the
    /// original hardware and a faster march: that speed-up is the genre's
    /// difficulty curve, so we keep it on purpose.
    pub fn march_interval(&self) -> u32 {
        let alive = self.alive_count() as u32;
        let wave_bonus = (self.wave - 1).min(3);
        (1 + alive / 2).saturating_sub(wave_bonus).max(1)
    }

    pub fn alien_bullet_frame(&self) -> &'static Sprite {
        &art().alien_bullet[(self.tick / 4 % 2) as usize]
    }

    // ---- lifecycle --------------------------------------------------------

    fn start_game(&mut self, events: &mut Vec<Event>) {
        self.score = 0;
        self.lives = START_LIVES;
        self.wave = 1;
        self.extra_life_given = false;
        self.setup_wave();
        self.phase = Phase::Playing;
        events.push(Event::GameStarted);
        events.push(Event::WaveStarted(1));
    }

    fn setup_wave(&mut self) {
        self.aliens.clear();
        for row in 0..ROWS {
            for col in 0..COLS {
                self.aliens.push(Alien {
                    kind: AlienKind::for_row(row),
                    row,
                    col,
                    alive: true,
                    dying: 0,
                });
            }
        }
        // Each new wave starts a row lower, up to a limit.
        let drop = (self.wave as i32 - 1).min(5) * MARCH_DY;
        self.formation = Formation {
            x: FORMATION_START_X,
            y: FORMATION_START_Y + drop,
            dir: 1,
            frame: 0,
            timer: 0,
        };
        self.shields = SHIELD_XS
            .iter()
            .map(|&x| Shield {
                x,
                y: SHIELD_Y,
                bitmap: art().shield.clone(),
            })
            .collect();
        self.player_x = (WIDTH - Self::player_w()) / 2;
        self.player_bullet = None;
        self.alien_bullets.clear();
        self.saucer = None;
        self.alien_fire_timer = 90;
        self.saucer_timer = 25 * TICKS_PER_SECOND;
    }

    // ---- the tick ---------------------------------------------------------

    pub fn update(&mut self, input: Input) -> Vec<Event> {
        let mut events = Vec::new();
        self.tick += 1;
        let fire_pressed = input.fire && !self.prev_fire;
        self.prev_fire = input.fire;

        match self.phase {
            Phase::Title => {
                if fire_pressed || input.start {
                    self.start_game(&mut events);
                }
            }
            Phase::GameOver(t) => {
                if t > 0 {
                    self.phase = Phase::GameOver(t - 1);
                } else if fire_pressed || input.start {
                    self.start_game(&mut events);
                }
            }
            Phase::PlayerDying(t) => {
                self.tick_explosions();
                if t > 0 {
                    self.phase = Phase::PlayerDying(t - 1);
                } else if self.lives == 0 {
                    self.phase = Phase::GameOver(GAME_OVER_LOCKOUT_TICKS);
                    events.push(Event::GameOver { score: self.score });
                } else {
                    self.player_x = (WIDTH - Self::player_w()) / 2;
                    self.alien_bullets.clear();
                    self.player_bullet = None;
                    self.alien_fire_timer = 60;
                    self.phase = Phase::Playing;
                }
            }
            Phase::WaveClear(t) => {
                self.tick_explosions();
                if t > 0 {
                    self.phase = Phase::WaveClear(t - 1);
                } else {
                    self.wave += 1;
                    self.setup_wave();
                    self.phase = Phase::Playing;
                    events.push(Event::WaveStarted(self.wave));
                }
            }
            Phase::Playing => self.step_playing(input, fire_pressed, &mut events),
        }
        events
    }

    fn step_playing(&mut self, input: Input, fire_pressed: bool, events: &mut Vec<Event>) {
        // 1. Player movement and firing. One shot on screen at a time: you
        //    have to wait for a miss to clear, which makes every shot count.
        let dx = input.right as i32 - input.left as i32;
        self.player_x =
            (self.player_x + dx * PLAYER_SPEED).clamp(MARGIN, WIDTH - MARGIN - Self::player_w());
        if fire_pressed && self.player_bullet.is_none() {
            self.player_bullet = Some(Bullet {
                x: self.player_x + Self::player_w() / 2,
                y: PLAYER_Y - 4,
            });
            events.push(Event::Shot);
        }

        // 2. Move everything that moves on its own.
        if let Some(b) = &mut self.player_bullet {
            b.y -= PLAYER_BULLET_SPEED;
            if b.y + (art().player_bullet.h as i32) < FIELD_TOP {
                self.player_bullet = None;
            }
        }
        for b in &mut self.alien_bullets {
            b.y += ALIEN_BULLET_SPEED;
        }
        self.alien_bullets.retain(|b| b.y < GROUND_Y);
        self.tick_explosions();
        self.march(events);
        if self.phase != Phase::Playing {
            return; // the formation landed
        }
        self.alien_fire();
        self.saucer_step();

        // 3. Collisions, then the consequences.
        self.collide(events);
        if self.score >= EXTRA_LIFE_SCORE && !self.extra_life_given {
            self.extra_life_given = true;
            self.lives += 1;
            events.push(Event::ExtraLife);
        }
        self.hi_score = self.hi_score.max(self.score);
        if self.phase == Phase::Playing && self.alive_count() == 0 {
            self.player_bullet = None;
            self.alien_bullets.clear();
            self.phase = Phase::WaveClear(WAVE_CLEAR_TICKS);
            events.push(Event::WaveCleared(self.wave));
        }
    }

    fn tick_explosions(&mut self) {
        for a in &mut self.aliens {
            a.dying = a.dying.saturating_sub(1);
        }
        if let Some(s) = &mut self.saucer {
            if s.dying > 0 {
                s.dying -= 1;
                if s.dying == 0 {
                    self.saucer = None;
                }
            }
        }
    }

    /// Living columns' horizontal extent, in screen pixels (sprite edges).
    fn formation_extent(&self) -> Option<(i32, i32)> {
        let mut extent: Option<(i32, i32)> = None;
        for a in self.aliens.iter().filter(|a| a.alive) {
            let (x, _) = self.alien_pos(a);
            let w = a.kind.frames()[0].w as i32;
            extent = Some(match extent {
                None => (x, x + w),
                Some((l, r)) => (l.min(x), r.max(x + w)),
            });
        }
        extent
    }

    fn march(&mut self, events: &mut Vec<Event>) {
        self.formation.timer += 1;
        if self.formation.timer < self.march_interval() {
            return;
        }
        self.formation.timer = 0;
        let Some((left, right)) = self.formation_extent() else {
            return;
        };
        let dir = self.formation.dir;
        let hits_wall =
            (dir > 0 && right + MARCH_DX > WIDTH - MARGIN) || (dir < 0 && left - MARCH_DX < MARGIN);
        if hits_wall {
            self.formation.y += MARCH_DY;
            self.formation.dir = -dir;
        } else {
            self.formation.x += dir * MARCH_DX;
        }
        self.formation.frame ^= 1;

        // Aliens chew through any shield they pass over.
        for i in 0..self.aliens.len() {
            if !self.aliens[i].alive {
                continue;
            }
            let (ax, ay) = self.alien_pos(&self.aliens[i]);
            let sprite = self.alien_sprite(&self.aliens[i]);
            for sh in &mut self.shields {
                for sy in 0..sprite.h as i32 {
                    for sx in 0..sprite.w as i32 {
                        if sprite.get(sx, sy) {
                            sh.bitmap.clear(ax + sx - sh.x, ay + sy - sh.y);
                        }
                    }
                }
            }
        }

        // Reaching the player's row ends the game outright.
        let lowest = self
            .aliens
            .iter()
            .filter(|a| a.alive)
            .map(|a| self.alien_pos(a).1 + 8)
            .max()
            .unwrap_or(0);
        if lowest > PLAYER_Y {
            self.lives = 0;
            self.player_bullet = None;
            self.alien_bullets.clear();
            self.phase = Phase::PlayerDying(PLAYER_DEATH_TICKS);
            events.push(Event::Invaded);
        }
    }

    /// The lowest living alien in each column gets to shoot. Half the time
    /// the column is the one nearest the player, so standing still is risky.
    fn alien_fire(&mut self) {
        if self.alien_fire_timer > 0 {
            self.alien_fire_timer -= 1;
            return;
        }
        if self.alien_bullets.len() >= MAX_ALIEN_BULLETS {
            return;
        }
        let mut shooters: Vec<usize> = Vec::new(); // index of lowest living alien per column
        for col in 0..COLS {
            if let Some(i) = (0..ROWS)
                .rev()
                .map(|row| row * COLS + col)
                .find(|&i| self.aliens[i].alive)
            {
                shooters.push(i);
            }
        }
        if shooters.is_empty() {
            return;
        }
        let player_cx = self.player_x + Self::player_w() / 2;
        let pick = if self.rng.chance(50) {
            *shooters
                .iter()
                .min_by_key(|&&i| {
                    let (x, _) = self.alien_pos(&self.aliens[i]);
                    (x + 6 - player_cx).abs()
                })
                .unwrap()
        } else {
            shooters[self.rng.below(shooters.len() as u32) as usize]
        };
        let a = &self.aliens[pick];
        let (x, y) = self.alien_pos(a);
        let w = a.kind.frames()[0].w as i32;
        self.alien_bullets.push(Bullet {
            x: x + w / 2 - 1,
            y: y + 8,
        });
        let base = 50u32.saturating_sub(6 * (self.wave - 1)).max(14);
        self.alien_fire_timer = base + self.rng.below(40);
    }

    fn saucer_step(&mut self) {
        match &mut self.saucer {
            Some(s) if s.dying == 0 => {
                s.x += s.dir;
                let w = art().saucer.w as i32;
                if s.x > WIDTH || s.x + w < 0 {
                    self.saucer = None;
                }
            }
            Some(_) => {}
            None => {
                self.saucer_timer = self.saucer_timer.saturating_sub(1);
                if self.saucer_timer == 0 && self.alive_count() >= 8 {
                    self.spawn_saucer();
                }
            }
        }
    }

    fn spawn_saucer(&mut self) {
        let from_left = self.rng.chance(50);
        let w = art().saucer.w as i32;
        let points = SAUCER_POINTS[self.rng.below(SAUCER_POINTS.len() as u32) as usize];
        self.saucer = Some(Saucer {
            x: if from_left { -w } else { WIDTH },
            dir: if from_left { 1 } else { -1 },
            points,
            dying: 0,
        });
        self.saucer_timer = 20 * TICKS_PER_SECOND + self.rng.below(10 * TICKS_PER_SECOND);
    }

    /// Erases a ragged crater around `(x, y)` in shield `si`.
    fn crater(&mut self, si: usize, x: i32, y: i32) {
        let sh = &mut self.shields[si];
        let (cx, cy) = (x - sh.x, y - sh.y);
        for dy in -2..=2i32 {
            for dx in -2..=2i32 {
                if dx.abs() + dy.abs() <= 2 {
                    sh.bitmap.clear(cx + dx, cy + dy);
                }
            }
        }
        // A few loose chips at radius 3 so craters don't look stamped.
        for _ in 0..3 {
            let dx = self.rng.below(7) as i32 - 3;
            let dy = self.rng.below(7) as i32 - 3;
            self.shields[si].bitmap.clear(cx + dx, cy + dy);
        }
    }

    fn shield_hit(&self, s: &Sprite, x: i32, y: i32) -> Option<(usize, i32, i32)> {
        self.shields.iter().enumerate().find_map(|(i, sh)| {
            overlap_at(s, x, y, &sh.bitmap, sh.x, sh.y).map(|(px, py)| (i, px, py))
        })
    }

    fn collide(&mut self, events: &mut Vec<Event>) {
        let a = art();
        let pb = &a.player_bullet;

        // Player shot vs aliens, the saucer, enemy shots, then shields.
        if let Some(b) = self.player_bullet {
            let hit_alien = (0..self.aliens.len()).find(|&i| {
                let al = &self.aliens[i];
                if !al.alive {
                    return false;
                }
                let (ax, ay) = self.alien_pos(al);
                overlap(pb, b.x, b.y, self.alien_sprite(al), ax, ay)
            });
            if let Some(i) = hit_alien {
                let al = &mut self.aliens[i];
                al.alive = false;
                al.dying = ALIEN_DEATH_TICKS;
                let points = al.kind.points();
                self.score += points;
                self.player_bullet = None;
                events.push(Event::AlienKilled {
                    kind: al.kind,
                    points,
                });
            } else if let Some(s) = self.saucer.as_mut().filter(|s| s.dying == 0) {
                if overlap(pb, b.x, b.y, &a.saucer, s.x, SAUCER_Y) {
                    s.dying = SAUCER_DEATH_TICKS;
                    self.score += s.points;
                    self.player_bullet = None;
                    events.push(Event::SaucerKilled { points: s.points });
                }
            }
        }
        if let Some(b) = self.player_bullet {
            let ab = &a.alien_bullet[0];
            if let Some(j) = self
                .alien_bullets
                .iter()
                .position(|e| Rect::of(pb, b.x, b.y).intersects(Rect::of(ab, e.x, e.y)))
            {
                self.alien_bullets.remove(j);
                self.player_bullet = None;
                events.push(Event::BulletsCollided);
            }
        }
        if let Some(b) = self.player_bullet {
            if let Some((si, x, y)) = self.shield_hit(pb, b.x, b.y) {
                self.crater(si, x, y);
                self.player_bullet = None;
                events.push(Event::ShieldHit);
            }
        }

        // Enemy shots vs shields, then the player.
        let frame = self.alien_bullet_frame();
        let mut j = 0;
        while j < self.alien_bullets.len() {
            let e = self.alien_bullets[j];
            if let Some((si, x, y)) = self.shield_hit(frame, e.x, e.y) {
                self.crater(si, x, y);
                self.alien_bullets.remove(j);
                events.push(Event::ShieldHit);
                continue;
            }
            if overlap(frame, e.x, e.y, &a.player, self.player_x, PLAYER_Y) {
                self.lives = self.lives.saturating_sub(1);
                self.alien_bullets.clear();
                self.player_bullet = None;
                self.phase = Phase::PlayerDying(PLAYER_DEATH_TICKS);
                events.push(Event::PlayerHit);
                return;
            }
            j += 1;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn playing(seed: u64) -> Game {
        let mut g = Game::new(seed);
        g.update(Input {
            start: true,
            ..Input::default()
        });
        assert_eq!(g.phase, Phase::Playing);
        g
    }

    #[test]
    fn saucer_crosses_and_can_be_shot() {
        let mut g = playing(3);
        g.alien_fire_timer = u32::MAX; // keep enemy fire out of this test
        g.saucer_timer = 1;
        g.update(Input::default());
        let s = g.saucer.clone().expect("saucer should spawn");
        assert!(SAUCER_POINTS.contains(&s.points));
        // Put a shot right under it and let it fly into the saucer.
        let x = g.saucer.as_ref().unwrap().x;
        g.saucer.as_mut().unwrap().x = x.clamp(40, 160);
        let sx = g.saucer.as_ref().unwrap().x;
        g.player_bullet = Some(Bullet {
            x: sx + 8 + s.dir * 2,
            y: SAUCER_Y + 12,
        });
        let mut killed = None;
        for _ in 0..10 {
            for e in g.update(Input::default()) {
                if let Event::SaucerKilled { points } = e {
                    killed = Some(points);
                }
            }
        }
        assert_eq!(killed, Some(s.points));
        assert!(g.score >= s.points);
        for _ in 0..SAUCER_DEATH_TICKS {
            g.update(Input::default());
        }
        assert!(
            g.saucer.is_none(),
            "wreck should disappear after its animation"
        );
    }

    #[test]
    fn enemy_fire_comes_from_the_lowest_alien_in_a_column() {
        let mut g = playing(5);
        // Leave only column 4, rows 0 and 1, alive: the row-1 alien must shoot.
        for a in &mut g.aliens {
            a.alive = a.col == 4 && a.row <= 1;
        }
        g.alien_fire_timer = 0;
        g.alien_fire();
        assert_eq!(g.alien_bullets.len(), 1);
        let shooter = g.aliens.iter().find(|a| a.col == 4 && a.row == 1).unwrap();
        let (_, y) = g.alien_pos(shooter);
        assert_eq!(g.alien_bullets[0].y, y + 8);
    }

    #[test]
    fn enemy_fire_is_capped() {
        let mut g = playing(5);
        for _ in 0..10 {
            g.alien_fire_timer = 0;
            g.alien_fire();
        }
        assert_eq!(g.alien_bullets.len(), MAX_ALIEN_BULLETS);
    }
}
