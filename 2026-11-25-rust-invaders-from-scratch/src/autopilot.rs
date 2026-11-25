//! A deliberately simple bot: dodge shots that are about to land, otherwise
//! line up under the nearest alien and fire. It plays well enough to clear
//! waves, which makes it a good end-to-end test of the whole game.

use crate::art::art;
use crate::game::*;

pub fn autopilot(g: &Game) -> Input {
    let mut input = Input::default();
    if g.phase != Phase::Playing {
        // Tap fire (press, release, press...) so menus see fresh presses.
        input.fire = g.tick.is_multiple_of(2);
        return input;
    }
    let pw = Game::player_w();
    let center = g.player_x + pw / 2;

    // Dodge: any enemy shot over us and close enough to arrive soon.
    let shot_w = art().alien_bullet[0].w as i32;
    let threat = g
        .alien_bullets
        .iter()
        .filter(|b| {
            b.y > PLAYER_Y - 70 && b.x + shot_w + 4 > g.player_x && b.x - 4 < g.player_x + pw
        })
        .map(|b| b.x + 1)
        .min_by_key(|&x| (x - center).abs());
    if let Some(bx) = threat {
        let go_right = bx <= center;
        let blocked_right = g.player_x + pw >= WIDTH - MARGIN;
        let blocked_left = g.player_x <= MARGIN;
        if (go_right && !blocked_right) || blocked_left {
            input.right = true;
        } else {
            input.left = true;
        }
        return input;
    }

    // Aim: the lowest living alien in the column nearest to us.
    let target = g
        .aliens
        .iter()
        .filter(|a| a.alive)
        .map(|a| {
            let (x, y) = g.alien_pos(a);
            (x + a.kind.frames()[0].w as i32 / 2, y)
        })
        .min_by_key(|&(x, y)| ((x - center).abs(), -y));
    if let Some((tx, ty)) = target {
        // Lead the target: by the time a shot climbs to it, the formation
        // will have stepped (flight ticks / march interval) more times.
        // Wall bounces are ignored, so near an edge the lead is a guess.
        let flight = (PLAYER_Y - 4 - (ty + 8)).max(0) / 4;
        let steps = flight / g.march_interval() as i32;
        let tx = (tx + g.formation.dir * 2 * steps).clamp(MARGIN, WIDTH - MARGIN);
        if tx > center + 1 {
            input.right = true;
        } else if tx < center - 1 {
            input.left = true;
        }
        if (tx - center).abs() <= 3 && g.player_bullet.is_none() {
            input.fire = g.tick.is_multiple_of(2);
        }
    }
    input
}
