//! Tiny deterministic roguelike engine: seeded dungeon generation, movement,
//! melee combat, monster chase AI. No dependencies, no I/O.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tile {
    Wall,
    Floor,
}

/// xorshift PRNG so dungeons are reproducible from a seed.
pub struct Rng(u64);
impl Rng {
    pub fn new(seed: u64) -> Self {
        Rng(seed.max(1))
    }
    pub fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.0 = x;
        x
    }
    /// Uniform-ish integer in lo..hi (hi exclusive).
    pub fn range(&mut self, lo: i32, hi: i32) -> i32 {
        lo + (self.next() % (hi - lo) as u64) as i32
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Rect {
    pub x: i32,
    pub y: i32,
    pub w: i32,
    pub h: i32,
}
impl Rect {
    pub fn center(&self) -> (i32, i32) {
        (self.x + self.w / 2, self.y + self.h / 2)
    }
    pub fn intersects(&self, o: &Rect) -> bool {
        self.x <= o.x + o.w && self.x + self.w >= o.x && self.y <= o.y + o.h && self.y + self.h >= o.y
    }
}

pub struct Map {
    pub width: i32,
    pub height: i32,
    pub tiles: Vec<Tile>,
    pub rooms: Vec<Rect>,
}

impl Map {
    pub fn idx(&self, x: i32, y: i32) -> usize {
        (y * self.width + x) as usize
    }
    pub fn in_bounds(&self, x: i32, y: i32) -> bool {
        x >= 0 && y >= 0 && x < self.width && y < self.height
    }
    pub fn walkable(&self, x: i32, y: i32) -> bool {
        self.in_bounds(x, y) && self.tiles[self.idx(x, y)] == Tile::Floor
    }
    fn carve_room(&mut self, r: &Rect) {
        for y in r.y..r.y + r.h {
            for x in r.x..r.x + r.w {
                let i = self.idx(x, y);
                self.tiles[i] = Tile::Floor;
            }
        }
    }
    fn carve_h(&mut self, x1: i32, x2: i32, y: i32) {
        for x in x1.min(x2)..=x1.max(x2) {
            let i = self.idx(x, y);
            self.tiles[i] = Tile::Floor;
        }
    }
    fn carve_v(&mut self, y1: i32, y2: i32, x: i32) {
        for y in y1.min(y2)..=y1.max(y2) {
            let i = self.idx(x, y);
            self.tiles[i] = Tile::Floor;
        }
    }

    /// Rooms-and-corridors generation: place non-overlapping random rooms,
    /// joining each to the previous one with an L-shaped corridor.
    pub fn generate(width: i32, height: i32, seed: u64) -> Map {
        let mut rng = Rng::new(seed);
        let mut m = Map { width, height, tiles: vec![Tile::Wall; (width * height) as usize], rooms: vec![] };
        for _ in 0..30 {
            let w = rng.range(4, 10);
            let h = rng.range(3, 7);
            let x = rng.range(1, width - w - 1);
            let y = rng.range(1, height - h - 1);
            let room = Rect { x, y, w, h };
            if m.rooms.iter().any(|r| r.intersects(&room)) {
                continue;
            }
            m.carve_room(&room);
            if let Some(prev) = m.rooms.last().copied() {
                let (px, py) = prev.center();
                let (nx, ny) = room.center();
                if rng.next() % 2 == 0 {
                    m.carve_h(px, nx, py);
                    m.carve_v(py, ny, nx);
                } else {
                    m.carve_v(py, ny, px);
                    m.carve_h(px, nx, ny);
                }
            }
            m.rooms.push(room);
        }
        m
    }
}

#[derive(Clone, Debug)]
pub struct Actor {
    pub x: i32,
    pub y: i32,
    pub hp: i32,
    pub power: i32,
    pub glyph: char,
}

pub struct Game {
    pub map: Map,
    pub player: Actor,
    pub monsters: Vec<Actor>,
    pub log: Vec<String>,
}

impl Game {
    pub fn new(seed: u64) -> Game {
        let map = Map::generate(60, 24, seed);
        let mut rng = Rng::new(seed ^ 0x9e3779b97f4a7c15);
        let (px, py) = map.rooms[0].center();
        let mut monsters = vec![];
        for r in map.rooms.iter().skip(1) {
            let (x, y) = (r.x + rng.range(0, r.w), r.y + rng.range(0, r.h));
            monsters.push(Actor { x, y, hp: 6, power: 2, glyph: 'o' });
        }
        Game { map, player: Actor { x: px, y: py, hp: 30, power: 3, glyph: '@' }, monsters, log: vec![] }
    }

    fn monster_at(&self, x: i32, y: i32) -> Option<usize> {
        self.monsters.iter().position(|m| m.x == x && m.y == y && m.hp > 0)
    }

    /// Player moves or attacks; then monsters act. Returns false if move was blocked.
    pub fn player_move(&mut self, dx: i32, dy: i32) -> bool {
        let (nx, ny) = (self.player.x + dx, self.player.y + dy);
        if let Some(i) = self.monster_at(nx, ny) {
            self.monsters[i].hp -= self.player.power;
            self.log.push(format!("You hit the orc for {}", self.player.power));
            if self.monsters[i].hp <= 0 {
                self.log.push("The orc dies".into());
            }
        } else if self.map.walkable(nx, ny) {
            self.player.x = nx;
            self.player.y = ny;
        } else {
            return false;
        }
        self.monsters_act();
        true
    }

    /// Monsters within 8 tiles step greedily toward the player, attacking when adjacent.
    fn monsters_act(&mut self) {
        for i in 0..self.monsters.len() {
            if self.monsters[i].hp <= 0 {
                continue;
            }
            let (mx, my) = (self.monsters[i].x, self.monsters[i].y);
            let (dx, dy) = (self.player.x - mx, self.player.y - my);
            if dx.abs().max(dy.abs()) > 8 {
                continue;
            }
            if dx.abs().max(dy.abs()) <= 1 {
                self.player.hp -= self.monsters[i].power;
                self.log.push(format!("The orc hits you for {}", self.monsters[i].power));
                continue;
            }
            // Try the dominant axis first, then the other.
            let (sx, sy) = (dx.signum(), dy.signum());
            let tries = if dx.abs() >= dy.abs() { [(sx, 0), (0, sy)] } else { [(0, sy), (sx, 0)] };
            for (tx, ty) in tries {
                if (tx, ty) == (0, 0) {
                    continue;
                }
                let (nx, ny) = (mx + tx, my + ty);
                let blocked = (nx, ny) == (self.player.x, self.player.y) || self.monster_at(nx, ny).is_some();
                if self.map.walkable(nx, ny) && !blocked {
                    self.monsters[i].x = nx;
                    self.monsters[i].y = ny;
                    break;
                }
            }
        }
    }

    pub fn is_dead(&self) -> bool {
        self.player.hp <= 0
    }

    pub fn render(&self) -> String {
        let mut out = String::new();
        for y in 0..self.map.height {
            for x in 0..self.map.width {
                let c = if (x, y) == (self.player.x, self.player.y) {
                    '@'
                } else if self.monster_at(x, y).is_some() {
                    'o'
                } else if self.map.walkable(x, y) {
                    '.'
                } else {
                    '#'
                };
                out.push(c);
            }
            out.push('\n');
        }
        out
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::VecDeque;

    #[test]
    fn same_seed_same_map() {
        assert_eq!(Map::generate(60, 24, 7).tiles, Map::generate(60, 24, 7).tiles);
        assert_ne!(Map::generate(60, 24, 7).tiles, Map::generate(60, 24, 8).tiles);
    }

    #[test]
    fn rooms_do_not_overlap() {
        let m = Map::generate(60, 24, 42);
        for (i, a) in m.rooms.iter().enumerate() {
            for b in &m.rooms[i + 1..] {
                assert!(!a.intersects(b));
            }
        }
    }

    #[test]
    fn all_rooms_connected() {
        for seed in 1..30 {
            let m = Map::generate(60, 24, seed);
            let (sx, sy) = m.rooms[0].center();
            let mut seen = vec![false; m.tiles.len()];
            let mut q = VecDeque::from([(sx, sy)]);
            seen[m.idx(sx, sy)] = true;
            while let Some((x, y)) = q.pop_front() {
                for (dx, dy) in [(1, 0), (-1, 0), (0, 1), (0, -1)] {
                    let (nx, ny) = (x + dx, y + dy);
                    if m.walkable(nx, ny) && !seen[m.idx(nx, ny)] {
                        seen[m.idx(nx, ny)] = true;
                        q.push_back((nx, ny));
                    }
                }
            }
            for r in &m.rooms {
                let (cx, cy) = r.center();
                assert!(seen[m.idx(cx, cy)], "seed {seed}: room unreachable");
            }
        }
    }

    #[test]
    fn walls_block_movement() {
        let mut g = Game::new(3);
        g.monsters.clear();
        let mut moved = 0;
        for _ in 0..500 {
            if g.player_move(-1, 0) {
                moved += 1;
            }
        }
        assert!(moved < 500);
        assert!(g.map.walkable(g.player.x, g.player.y));
    }

    #[test]
    fn player_attacks_and_kills_monster() {
        let mut g = Game::new(5);
        g.monsters = vec![Actor { x: g.player.x + 1, y: g.player.y, hp: 6, power: 0, glyph: 'o' }];
        g.player_move(1, 0);
        assert_eq!(g.monsters[0].hp, 3);
        g.player_move(1, 0);
        assert!(g.monsters[0].hp <= 0);
        assert!(g.log.iter().any(|l| l.contains("dies")));
    }

    #[test]
    fn monster_chases_and_hurts_player() {
        let mut g = Game::new(5);
        let (px, py) = (g.player.x, g.player.y);
        // Put a monster on the first walkable tile 3 away along a free row, else skip search.
        let spot = (2..6).find(|d| (1..=*d).all(|k| g.map.walkable(px + k, py)));
        let d = spot.expect("room is wide enough");
        g.monsters = vec![Actor { x: px + d, y: py, hp: 6, power: 2, glyph: 'o' }];
        let hp = g.player.hp;
        for _ in 0..(d + 1) {
            g.monsters_act();
        }
        assert!(g.player.hp < hp);
    }

    #[test]
    fn render_dimensions() {
        let g = Game::new(1);
        let r = g.render();
        assert_eq!(r.lines().count(), 24);
        assert!(r.lines().all(|l| l.len() == 60));
        assert!(r.contains('@'));
    }
}
