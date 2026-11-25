//! Behaviour tests: drive the public game API tick by tick, as the window
//! and the headless runner do, and check what a player would see.

use starfall::art::art;
use starfall::autopilot::autopilot;
use starfall::game::*;
use starfall::render::{self, new_buffer, render};

const IDLE: Input = Input {
    left: false,
    right: false,
    fire: false,
    start: false,
};
const FIRE: Input = Input {
    left: false,
    right: false,
    fire: true,
    start: false,
};

fn playing(seed: u64) -> Game {
    let mut g = Game::new(seed);
    let ev = g.update(Input {
        start: true,
        ..IDLE
    });
    assert_eq!(ev, vec![Event::GameStarted, Event::WaveStarted(1)]);
    g
}

/// Runs idle ticks until `pred` matches an event, returning that event.
fn run_until(g: &mut Game, max: u32, input: Input, pred: impl Fn(&Event) -> bool) -> Option<Event> {
    for _ in 0..max {
        if let Some(e) = g.update(input).into_iter().find(|e| pred(e)) {
            return Some(e);
        }
    }
    None
}

fn shield_pixels(g: &Game) -> usize {
    g.shields.iter().map(|s| s.bitmap.count()).sum()
}

#[test]
fn a_new_game_starts_on_the_title_screen() {
    let g = Game::new(1);
    assert_eq!(g.phase, Phase::Title);
    let g = playing(1);
    assert_eq!(g.phase, Phase::Playing);
    assert_eq!(g.aliens.len(), ROWS * COLS);
    assert_eq!(g.alive_count(), 55);
    assert_eq!((g.lives, g.score, g.wave), (START_LIVES, 0, 1));
    assert_eq!(g.shields.len(), 4);
    // Point values by row: one row of 30s, two of 20s, two of 10s.
    let points: Vec<u32> = (0..ROWS)
        .map(|r| g.aliens[r * COLS].kind.points())
        .collect();
    assert_eq!(points, [30, 20, 20, 10, 10]);
}

#[test]
fn the_player_moves_and_stops_at_the_margins() {
    let mut g = playing(1);
    let x0 = g.player_x;
    g.update(Input {
        right: true,
        ..IDLE
    });
    assert_eq!(g.player_x, x0 + 1);
    g.update(Input {
        left: true,
        right: true,
        ..IDLE
    });
    assert_eq!(g.player_x, x0 + 1, "both keys cancel out");
    for _ in 0..300 {
        g.update(Input { left: true, ..IDLE });
    }
    assert_eq!(g.player_x, MARGIN);
    for _ in 0..300 {
        g.update(Input {
            right: true,
            ..IDLE
        });
    }
    assert_eq!(g.player_x, WIDTH - MARGIN - Game::player_w());
}

#[test]
fn holding_fire_shoots_once_and_only_one_shot_flies_at_a_time() {
    let mut g = playing(1);
    assert!(g.update(FIRE).contains(&Event::Shot));
    // Holding the key is not a new press.
    for _ in 0..5 {
        assert!(!g.update(FIRE).contains(&Event::Shot));
    }
    // A fresh press while the shot is still in the air does nothing.
    g.update(IDLE);
    assert!(g.player_bullet.is_some());
    assert!(!g.update(FIRE).contains(&Event::Shot));
}

#[test]
fn a_shot_through_a_shield_gap_kills_the_bottom_alien_of_that_column() {
    let mut g = playing(1);
    // Column 2 is centred at x = 64, between the first two shields.
    g.player_x = 64 - Game::player_w() / 2;
    g.update(FIRE);
    let e = run_until(&mut g, 60, IDLE, |e| matches!(e, Event::AlienKilled { .. }));
    assert_eq!(
        e,
        Some(Event::AlienKilled {
            kind: AlienKind::Brute,
            points: 10
        })
    );
    let dead: Vec<(usize, usize)> = g
        .aliens
        .iter()
        .filter(|a| !a.alive)
        .map(|a| (a.row, a.col))
        .collect();
    assert_eq!(dead, [(4, 2)]);
    assert_eq!(g.score, 10);
    assert_eq!(g.hi_score, 10);
    assert!(g.player_bullet.is_none(), "the shot is used up");
    // The explosion is drawn for a while, then the alien is gone.
    assert!(g.aliens[4 * COLS + 2].dying > 0);
}

#[test]
fn shots_erode_shields_from_both_sides() {
    let mut g = playing(1);
    let before = shield_pixels(&g);
    // Fire straight up into the middle of the first shield.
    g.player_x = SHIELD_XS[0] + 11 - Game::player_w() / 2;
    g.update(FIRE);
    let e = run_until(&mut g, 30, IDLE, |e| *e == Event::ShieldHit);
    assert!(e.is_some(), "the shot should hit the shield");
    let after_player = shield_pixels(&g);
    assert!(after_player < before);
    assert!(g.player_bullet.is_none());
    assert_eq!(g.score, 0);

    // An enemy shot dropped onto the last shield takes a bite from the top.
    g.alien_bullets.push(Bullet {
        x: SHIELD_XS[3] + 10,
        y: SHIELD_Y - 10,
    });
    assert!(run_until(&mut g, 20, IDLE, |e| *e == Event::ShieldHit).is_some());
    assert!(shield_pixels(&g) < after_player);
}

#[test]
fn the_formation_marches_to_the_wall_then_drops_and_turns() {
    let mut g = playing(1);
    let (x0, y0) = (g.formation.x, g.formation.y);
    let mut turned = false;
    for _ in 0..5000 {
        g.alien_bullets.clear(); // the march is what we're testing, not dodging
        g.update(IDLE);
        assert_eq!(g.phase, Phase::Playing);
        if g.formation.dir == -1 {
            turned = true;
            break;
        }
        assert_eq!(g.formation.y, y0, "no drop before reaching the wall");
    }
    assert!(turned);
    assert!(g.formation.x > x0);
    assert_eq!(g.formation.y, y0 + 8);
    // The rightmost alien stopped short of the right margin.
    let right = g
        .aliens
        .iter()
        .map(|a| g.alien_pos(a).0 + a.kind.frames()[0].w as i32)
        .max()
        .unwrap();
    assert!(right <= WIDTH - MARGIN);
}

#[test]
fn fewer_aliens_march_faster() {
    let mut g = playing(1);
    let full = g.march_interval();
    for a in g.aliens.iter_mut().take(40) {
        a.alive = false;
    }
    let some = g.march_interval();
    for a in g.aliens.iter_mut().take(54) {
        a.alive = false;
    }
    let last = g.march_interval();
    assert!(full > some && some > last, "{full} > {some} > {last}");
    assert_eq!(last, 1, "the last alien steps every tick");
}

#[test]
fn getting_hit_costs_a_life_and_respawns_the_player_in_the_middle() {
    let mut g = playing(1);
    g.player_x = 30;
    g.alien_bullets.push(Bullet {
        x: 30 + 5,
        y: PLAYER_Y - 4,
    });
    let ev = g.update(IDLE);
    assert!(ev.contains(&Event::PlayerHit), "{ev:?}");
    assert_eq!(g.lives, START_LIVES - 1);
    assert!(matches!(g.phase, Phase::PlayerDying(_)));
    assert!(g.alien_bullets.is_empty());
    // The world is frozen while the wreck animates.
    let fx = g.formation.x;
    for _ in 0..200 {
        g.update(IDLE);
        if g.phase == Phase::Playing {
            break;
        }
        assert_eq!(g.formation.x, fx);
    }
    assert_eq!(g.phase, Phase::Playing);
    assert_eq!(g.player_x, (WIDTH - Game::player_w()) / 2);
}

#[test]
fn losing_the_last_life_ends_the_game_and_fire_restarts_it() {
    let mut g = playing(1);
    g.score = 120;
    g.hi_score = 120;
    for life in (0..START_LIVES).rev() {
        let x = g.player_x;
        g.alien_bullets.push(Bullet {
            x: x + 5,
            y: PLAYER_Y - 4,
        });
        assert!(g.update(IDLE).contains(&Event::PlayerHit));
        assert_eq!(g.lives, life);
        let mut events = Vec::new();
        while matches!(g.phase, Phase::PlayerDying(_)) {
            events.extend(g.update(IDLE));
        }
        let over = events.contains(&Event::GameOver { score: 120 });
        assert_eq!(over, life == 0, "game over only after the last life");
    }
    assert!(matches!(g.phase, Phase::GameOver(_)));
    // Fire held through the game-over screen does nothing until released and pressed again,
    // and not before the lockout runs out.
    assert!(run_until(&mut g, 200, FIRE, |e| *e == Event::GameStarted).is_none());
    g.update(IDLE);
    assert!(g.update(FIRE).contains(&Event::GameStarted));
    assert_eq!((g.score, g.lives, g.wave), (0, START_LIVES, 1));
    assert_eq!(g.hi_score, 120, "the high score survives a restart");
}

#[test]
fn aliens_reaching_the_player_end_the_game_regardless_of_lives() {
    let mut g = playing(1);
    g.lives = 5;
    g.formation.y = PLAYER_Y - 4 * CELL_H - 6; // bottom row just above the player line
    g.alien_bullets.clear();
    let e = run_until(&mut g, 400, IDLE, |e| *e == Event::Invaded);
    assert_eq!(e, Some(Event::Invaded));
    assert_eq!(g.lives, 0);
    assert!(run_until(&mut g, 200, IDLE, |e| matches!(e, Event::GameOver { .. })).is_some());
}

#[test]
fn clearing_a_wave_starts_the_next_one_lower_down() {
    let mut g = playing(1);
    let y1 = g.formation.y;
    for a in g.aliens.iter_mut().skip(1) {
        a.alive = false;
    }
    // Shoot the last one (row 0, col 0) by teleporting a shot under it.
    let (ax, ay) = g.alien_pos(&g.aliens[0]);
    g.player_bullet = Some(Bullet {
        x: ax + 4,
        y: ay + 10,
    });
    let ev = run_until(&mut g, 10, IDLE, |e| matches!(e, Event::WaveCleared(_)));
    assert_eq!(ev, Some(Event::WaveCleared(1)));
    assert!(matches!(g.phase, Phase::WaveClear(_)));
    assert_eq!(
        run_until(&mut g, 300, IDLE, |e| matches!(e, Event::WaveStarted(_))),
        Some(Event::WaveStarted(2))
    );
    assert_eq!(g.alive_count(), 55);
    assert_eq!(g.formation.y, y1 + 8);
    assert_eq!(
        shield_pixels(&g),
        4 * art().shield.count(),
        "shields are rebuilt"
    );
}

#[test]
fn shots_cancel_each_other_out() {
    let mut g = playing(1);
    g.player_bullet = Some(Bullet { x: 100, y: 150 });
    g.alien_bullets.push(Bullet { x: 99, y: 140 });
    let e = run_until(&mut g, 5, IDLE, |e| *e == Event::BulletsCollided);
    assert!(e.is_some());
    assert!(g.player_bullet.is_none() && g.alien_bullets.is_empty());
}

#[test]
fn an_extra_life_is_awarded_once() {
    let mut g = playing(1);
    g.score = EXTRA_LIFE_SCORE - 10;
    g.player_x = 64 - Game::player_w() / 2;
    g.update(FIRE);
    let e = run_until(&mut g, 60, IDLE, |e| *e == Event::ExtraLife);
    assert!(e.is_some());
    assert_eq!(g.lives, START_LIVES + 1);
    g.score += 5000;
    assert!(run_until(&mut g, 30, IDLE, |e| *e == Event::ExtraLife).is_none());
}

#[test]
fn the_same_seed_replays_the_same_game() {
    let run = |seed| {
        let mut g = Game::new(seed);
        let mut log = Vec::new();
        for _ in 0..4000 {
            let ev = g.update(autopilot(&g));
            log.extend(ev);
        }
        (
            g.score,
            g.tick,
            g.player_x,
            g.formation.x,
            g.formation.y,
            log,
        )
    };
    assert_eq!(run(11), run(11));
    assert_ne!(
        run(11).5,
        run(12).5,
        "different seeds should play out differently"
    );
}

#[test]
fn the_autopilot_can_clear_the_first_wave() {
    // End-to-end: movement, firing, marching, enemy fire, shields and scoring
    // all have to work for a bot this simple to win a wave.
    let mut cleared = 0;
    for seed in 1..=5 {
        let mut g = Game::new(seed);
        for _ in 0..20_000 {
            let ev = g.update(autopilot(&g));
            if ev.contains(&Event::WaveCleared(1)) {
                cleared += 1;
                assert_eq!(g.score % 10, 0);
                assert!(g.score >= 990, "a full wave is worth 990 before saucers");
                break;
            }
            if matches!(g.phase, Phase::GameOver(_)) {
                break;
            }
        }
    }
    assert!(
        cleared >= 4,
        "autopilot cleared wave 1 in only {cleared}/5 games"
    );
}

#[test]
fn the_renderer_draws_the_title_and_the_play_field() {
    let mut buf = new_buffer();
    let g = Game::new(1);
    render(&g, &mut buf);
    let lit = buf.px.iter().filter(|&&p| p != render::BACKGROUND).count();
    assert!(
        lit > 500,
        "title screen should have text and sprites, got {lit} lit pixels"
    );

    let g = playing(1);
    render(&g, &mut buf);
    // The ground line spans the screen.
    assert!((0..WIDTH).all(|x| buf.get(x, GROUND_Y) == Some(render::PLAYER)));
    // Every set pixel of the first alien's sprite is drawn in its colour.
    let a = &g.aliens[0];
    let (ax, ay) = g.alien_pos(a);
    let s = g.alien_sprite(a);
    for y in 0..s.h as i32 {
        for x in 0..s.w as i32 {
            if s.get(x, y) {
                assert_eq!(buf.get(ax + x, ay + y), Some(render::alien_color(a.kind)));
            }
        }
    }
    // The player is drawn at its position.
    assert_eq!(buf.get(g.player_x + 6, PLAYER_Y), Some(render::PLAYER));
}
