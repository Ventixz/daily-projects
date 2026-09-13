use evolution_sim::world::World;
use evolution_sim::Simulation;
use rand::rngs::StdRng;
use rand::SeedableRng;
use std::f32::consts::PI;

struct Args {
    generations: usize,
    population: usize,
    food: usize,
    seed: Option<u64>,
}

impl Args {
    fn parse(mut args: impl Iterator<Item = String>) -> Self {
        let mut parsed = Self {
            generations: 100,
            population: 40,
            food: 40,
            seed: None,
        };

        while let Some(flag) = args.next() {
            let value = args.next();

            match (flag.as_str(), value) {
                ("--generations", Some(v)) => parsed.generations = v.parse().unwrap_or(parsed.generations),
                ("--population", Some(v)) => parsed.population = v.parse().unwrap_or(parsed.population),
                ("--food", Some(v)) => parsed.food = v.parse().unwrap_or(parsed.food),
                ("--seed", Some(v)) => parsed.seed = v.parse().ok(),
                _ => {}
            }
        }

        parsed
    }
}

fn main() {
    let args = Args::parse(std::env::args().skip(1));

    let mut rng = match args.seed {
        Some(seed) => StdRng::seed_from_u64(seed),
        None => StdRng::from_entropy(),
    };

    let mut sim = Simulation::random(&mut rng, args.population, args.food);

    println!("gen    min      avg      max");
    for generation in 1..=args.generations {
        let stats = sim.train(&mut rng);
        println!(
            "{generation:>3}   {:>6.2}   {:>6.2}   {:>6.2}",
            stats.min_fitness, stats.avg_fitness, stats.max_fitness
        );
    }

    println!();
    println!("final snapshot ('.' = food, arrow = an animal's heading):");
    println!("{}", render(sim.world()));
}

fn render(world: &World) -> String {
    const WIDTH: usize = 60;
    const HEIGHT: usize = 24;

    let mut grid = vec![vec![' '; WIDTH]; HEIGHT];

    for food in world.foods() {
        let (x, y) = to_cell(food.position().x, food.position().y, WIDTH, HEIGHT);
        grid[y][x] = '.';
    }

    for animal in world.animals() {
        let (x, y) = to_cell(animal.position().x, animal.position().y, WIDTH, HEIGHT);
        grid[y][x] = heading_char(animal.rotation());
    }

    grid.into_iter()
        .map(|row| row.into_iter().collect::<String>())
        .collect::<Vec<_>>()
        .join("\n")
}

fn to_cell(x: f32, y: f32, width: usize, height: usize) -> (usize, usize) {
    let cx = ((x * width as f32) as usize).min(width - 1);
    let cy = ((y * height as f32) as usize).min(height - 1);
    (cx, cy)
}

fn heading_char(rotation: f32) -> char {
    let normalized = rotation.rem_euclid(2.0 * PI);
    let octant = (normalized / (PI / 4.0)).round() as i64 % 8;

    match octant {
        0 => '>',
        1 => '\\',
        2 => 'v',
        3 => '/',
        4 => '<',
        5 => '\\',
        6 => '^',
        _ => '/',
    }
}
