use robinson::layout::Dimensions;
use robinson::{css, html_parser, layout, painting, style};
use std::env;
use std::fs;
use std::process;

fn usage(program: &str) -> ! {
    eprintln!(
        "usage: {} <page.html> <page.css> <out.ppm> [viewport_width] [viewport_height]",
        program
    );
    process::exit(1);
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() < 4 {
        usage(&args[0]);
    }

    let html = fs::read_to_string(&args[1]).unwrap_or_else(|e| {
        eprintln!("failed to read {}: {}", args[1], e);
        process::exit(1);
    });
    let css_src = fs::read_to_string(&args[2]).unwrap_or_else(|e| {
        eprintln!("failed to read {}: {}", args[2], e);
        process::exit(1);
    });
    let out_path = &args[3];

    let width: f32 = args.get(4).and_then(|s| s.parse().ok()).unwrap_or(800.0);
    let height: f32 = args.get(5).and_then(|s| s.parse().ok()).unwrap_or(600.0);

    let dom_root = html_parser::parse(html);
    let stylesheet = css::parse(css_src);
    let style_root = style::style_tree(&dom_root, &stylesheet);

    let mut viewport = Dimensions::default();
    viewport.content.width = width;
    viewport.content.height = height;

    let layout_root = layout::layout_tree(&style_root, viewport);

    // The page can be taller than the viewport (normal flow only grows
    // downward); paint the whole thing rather than clipping content away.
    let mut page_bounds = viewport.content;
    page_bounds.height = layout_root
        .dimensions
        .margin_box()
        .height
        .max(viewport.content.height);

    let canvas = painting::paint(&layout_root, page_bounds);

    fs::write(out_path, canvas.to_ppm()).unwrap_or_else(|e| {
        eprintln!("failed to write {}: {}", out_path, e);
        process::exit(1);
    });

    println!(
        "wrote {} ({}x{}) from {} + {}",
        out_path, width, page_bounds.height, args[1], args[2]
    );
}
