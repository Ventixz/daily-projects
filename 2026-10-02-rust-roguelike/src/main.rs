use rust_roguelike::Game;

/// Headless demo: the player walks toward the nearest orc and fights it.
fn main() {
    let seed = std::env::args().nth(1).and_then(|s| s.parse().ok()).unwrap_or(2026);
    let mut g = Game::new(seed);
    println!("{}", g.render());
    for _ in 0..300 {
        if g.is_dead() || g.monsters.iter().all(|m| m.hp <= 0) {
            break;
        }
        let (px, py) = (g.player.x, g.player.y);
        let target = g.monsters.iter().filter(|m| m.hp > 0).min_by_key(|m| (m.x - px).abs() + (m.y - py).abs()).unwrap();
        let (dx, dy) = ((target.x - px).signum(), (target.y - py).signum());
        if !g.player_move(dx, 0) {
            g.player_move(0, dy);
        }
    }
    println!("{}", g.render());
    println!("player hp: {}, orcs alive: {}", g.player.hp, g.monsters.iter().filter(|m| m.hp > 0).count());
    for l in g.log.iter().rev().take(5).rev() {
        println!("{l}");
    }
}
