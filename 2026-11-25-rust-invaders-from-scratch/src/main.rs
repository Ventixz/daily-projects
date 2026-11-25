//! Starfall's entry point: a desktop window by default, or a headless run
//! driven by the autopilot that prints a summary and can save screenshots.

use starfall::autopilot::autopilot;
use starfall::game::{Event, Game, Input, Phase, HEIGHT, WIDTH};
use starfall::png;
use starfall::render::{new_buffer, render};
use std::process::ExitCode;

const USAGE: &str = "\
starfall - an invaders-style shooter built from scratch

USAGE:
    starfall                     play in a window (arrows/A/D move, space fires,
                                 P pauses, Esc quits)
    starfall --headless [OPTIONS]
                                 let the autopilot play, print a summary

OPTIONS:
    --seed N        game seed (default: clock in a window, 1 headless)
    --ticks N       headless: stop after N ticks at 60/s (default 36000 = 10 min)
    --shot FILE     headless: write the final frame as a PNG
    --shot-at N     headless: take the --shot at tick N instead of the end
    --title         headless: stay on the title screen (for a screenshot)
    -h, --help      show this help
";

struct Options {
    headless: bool,
    seed: Option<u64>,
    ticks: u64,
    shot: Option<String>,
    shot_at: Option<u64>,
    title: bool,
}

fn parse(args: &[String]) -> Result<Options, String> {
    let mut o = Options {
        headless: false,
        seed: None,
        ticks: 36_000,
        shot: None,
        shot_at: None,
        title: false,
    };
    let mut it = args.iter();
    while let Some(a) = it.next() {
        let mut value = |name: &str| {
            it.next()
                .cloned()
                .ok_or_else(|| format!("{name} needs a value"))
        };
        match a.as_str() {
            "--headless" => o.headless = true,
            "--title" => o.title = true,
            "--seed" => {
                o.seed = Some(
                    value("--seed")?
                        .parse()
                        .map_err(|_| "--seed must be a number")?,
                )
            }
            "--ticks" => {
                o.ticks = value("--ticks")?
                    .parse()
                    .map_err(|_| "--ticks must be a number")?
            }
            "--shot" => o.shot = Some(value("--shot")?),
            "--shot-at" => {
                o.shot_at = Some(
                    value("--shot-at")?
                        .parse()
                        .map_err(|_| "--shot-at must be a number")?,
                )
            }
            "-h" | "--help" => return Err(String::new()),
            other => return Err(format!("unknown argument: {other}")),
        }
    }
    Ok(o)
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let opts = match parse(&args) {
        Ok(o) => o,
        Err(e) if e.is_empty() => {
            print!("{USAGE}");
            return ExitCode::SUCCESS;
        }
        Err(e) => {
            eprintln!("error: {e}\n\n{USAGE}");
            return ExitCode::from(2);
        }
    };
    let result = if opts.headless {
        headless(&opts)
    } else {
        windowed(&opts)
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}

fn save_png(game: &Game, path: &str) -> Result<(), String> {
    let mut buf = new_buffer();
    render(game, &mut buf);
    let data = png::encode_rgb(WIDTH as u32, HEIGHT as u32, &buf.to_rgb_bytes());
    std::fs::write(path, data).map_err(|e| format!("writing {path}: {e}"))?;
    println!("wrote {path}");
    Ok(())
}

fn headless(o: &Options) -> Result<(), String> {
    let mut game = Game::new(o.seed.unwrap_or(1));
    let (mut kills, mut shots, mut deaths, mut waves) = (0u32, 0u32, 0u32, 0u32);
    for t in 1..=o.ticks {
        let input = if o.title {
            Input::default()
        } else {
            autopilot(&game)
        };
        for e in game.update(input) {
            match e {
                Event::AlienKilled { .. } | Event::SaucerKilled { .. } => kills += 1,
                Event::Shot => shots += 1,
                Event::PlayerHit | Event::Invaded => deaths += 1,
                Event::WaveCleared(_) => waves += 1,
                _ => {}
            }
        }
        if let (Some(path), Some(at)) = (&o.shot, o.shot_at) {
            if t == at {
                save_png(&game, path)?;
            }
        }
        if matches!(game.phase, Phase::GameOver(_)) && !o.title {
            break;
        }
    }
    let secs = game.tick as f64 / 60.0;
    println!(
        "ticks {} ({secs:.0}s)  score {}  wave {}  waves cleared {waves}  kills {kills}  shots {shots}  \
         hit rate {:.0}%  lives lost {deaths}  phase {:?}",
        game.tick,
        game.score,
        game.wave,
        if shots == 0 { 0.0 } else { kills as f64 * 100.0 / shots as f64 },
        game.phase
    );
    if let (Some(path), None) = (&o.shot, o.shot_at) {
        save_png(&game, path)?;
    }
    Ok(())
}

#[cfg(feature = "window")]
fn windowed(o: &Options) -> Result<(), String> {
    use minifb::{Key, KeyRepeat, Scale, Window, WindowOptions};
    use starfall::font::draw_centered;
    use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

    let seed = o.seed.unwrap_or_else(|| {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_nanos() as u64)
            .unwrap_or(1)
    });
    let (w, h) = (WIDTH as usize, HEIGHT as usize);
    let mut window = Window::new(
        "Starfall",
        w,
        h,
        WindowOptions {
            scale: Scale::X2,
            ..WindowOptions::default()
        },
    )
    .map_err(|e| format!("opening window: {e}"))?;
    window.set_target_fps(60);

    let mut game = Game::new(seed);
    let mut buf = new_buffer();
    let mut paused = false;

    // Fixed timestep: the game always advances in 1/60 s ticks, however fast
    // or slow frames arrive. Leftover time carries over to the next frame.
    let step = Duration::from_nanos(1_000_000_000 / 60);
    let mut last = Instant::now();
    let mut lag = Duration::ZERO;

    while window.is_open() && !window.is_key_down(Key::Escape) {
        if window.is_key_pressed(Key::P, KeyRepeat::No) && game.phase == Phase::Playing {
            paused = !paused;
        }
        let down = |k: Key| window.is_key_down(k);
        let input = Input {
            left: down(Key::Left) || down(Key::A),
            right: down(Key::Right) || down(Key::D),
            fire: down(Key::Space) || down(Key::Up) || down(Key::W),
            start: down(Key::Enter),
        };

        let now = Instant::now();
        lag += now - last;
        last = now;
        lag = lag.min(step * 5); // after a stall, don't fast-forward for ages
        while lag >= step {
            if !paused {
                game.update(input);
            }
            lag -= step;
        }

        render(&game, &mut buf);
        if paused {
            draw_centered(&mut buf, "PAUSED", 140, starfall::render::ACCENT);
        }
        window
            .update_with_buffer(&buf.px, w, h)
            .map_err(|e| format!("drawing: {e}"))?;
    }
    Ok(())
}

#[cfg(not(feature = "window"))]
fn windowed(_: &Options) -> Result<(), String> {
    Err(
        "built without the `window` feature; run with --headless, or rebuild with default features"
            .into(),
    )
}
