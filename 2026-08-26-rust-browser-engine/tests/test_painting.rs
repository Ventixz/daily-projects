use robinson::css::{parse as parse_css, Color};
use robinson::html_parser::parse as parse_html;
use robinson::layout::{layout_tree, Dimensions};
use robinson::painting::paint;
use robinson::style::style_tree;

fn viewport(width: f32, height: f32) -> Dimensions {
    let mut d = Dimensions::default();
    d.content.width = width;
    d.content.height = height;
    d
}

#[test]
fn canvas_has_requested_dimensions() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css("div { display: block; }".to_string());
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(20.0, 10.0));
    let canvas = paint(&root, viewport(20.0, 10.0).content);
    assert_eq!(canvas.width, 20);
    assert_eq!(canvas.height, 10);
    assert_eq!(canvas.pixels.len(), 200);
}

#[test]
fn default_canvas_is_white() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css("div { display: block; }".to_string());
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(10.0, 10.0));
    let canvas = paint(&root, viewport(10.0, 10.0).content);
    assert_eq!(canvas.pixels[0], Color::rgb(255, 255, 255));
}

#[test]
fn solid_background_fills_the_border_box() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css(
        "div { display: block; width: 4px; height: 4px; background: #ff0000; }".to_string(),
    );
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(10.0, 10.0));
    let canvas = paint(&root, viewport(10.0, 10.0).content);

    // top-left 4x4 block should be red.
    assert_eq!(canvas.pixels[0], Color::rgb(255, 0, 0));
    assert_eq!(canvas.pixels[3 * canvas.width + 3], Color::rgb(255, 0, 0));
    // just outside the box should still be white.
    assert_eq!(canvas.pixels[4], Color::rgb(255, 255, 255));
}

#[test]
fn border_is_painted_around_the_content() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css(
        "div { display: block; width: 6px; height: 6px; border-width: 2px; border-color: #0000ff; }"
            .to_string(),
    );
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(20.0, 20.0));
    let canvas = paint(&root, viewport(20.0, 20.0).content);

    // Top-left pixel is inside the 2px border.
    assert_eq!(canvas.pixels[0], Color::rgb(0, 0, 255));
    // Pixel at (5,5) is inside the border box but past the border into
    // content (border box is 10x10: 2 border + 6 content + 2 border), so
    // it should be untouched (white), since content itself has no background.
    let x = 5;
    let y = 5;
    assert_eq!(
        canvas.pixels[y * canvas.width + x],
        Color::rgb(255, 255, 255)
    );
}

#[test]
fn to_ppm_has_correct_binary_header_and_body_length() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css("div { display: block; }".to_string());
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(3.0, 2.0));
    let canvas = paint(&root, viewport(3.0, 2.0).content);
    let ppm = canvas.to_ppm();
    let header = b"P6\n3 2\n255\n";
    assert!(ppm.starts_with(header));
    // header + 3*2 pixels * 3 bytes (RGB) each.
    assert_eq!(ppm.len(), header.len() + 3 * 2 * 3);
}
