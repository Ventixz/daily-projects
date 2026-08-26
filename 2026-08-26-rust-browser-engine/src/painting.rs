//! Rasterizes a layout tree into a `Canvas` of RGBA pixels via a display
//! list (background rectangle, then the four border rectangles, per box —
//! painted in tree order so later siblings/children draw over earlier
//! ones, which is correct for the non-overlapping boxes normal flow
//! produces).

use crate::css::{Color, Value::ColorValue};
use crate::layout::{BoxType, LayoutBox, Rect};

#[derive(Debug, Clone)]
pub enum DisplayCommand {
    SolidColor(Color, Rect),
}

pub type DisplayList = Vec<DisplayCommand>;

pub fn build_display_list(layout_root: &LayoutBox) -> DisplayList {
    let mut list = Vec::new();
    render_layout_box(&mut list, layout_root);
    list
}

fn render_layout_box(list: &mut DisplayList, layout_box: &LayoutBox) {
    render_background(list, layout_box);
    render_borders(list, layout_box);
    for child in &layout_box.children {
        render_layout_box(list, child);
    }
}

fn get_style_color(layout_box: &LayoutBox, name: &str) -> Option<Color> {
    match layout_box.box_type {
        BoxType::BlockNode(style) | BoxType::InlineNode(style) => match style.value(name) {
            Some(ColorValue(color)) => Some(color),
            _ => None,
        },
        BoxType::AnonymousBlock => None,
    }
}

fn render_background(list: &mut DisplayList, layout_box: &LayoutBox) {
    if let Some(color) = get_style_color(layout_box, "background") {
        list.push(DisplayCommand::SolidColor(
            color,
            layout_box.dimensions.border_box(),
        ));
    }
}

fn render_borders(list: &mut DisplayList, layout_box: &LayoutBox) {
    let color = match get_style_color(layout_box, "border-color") {
        Some(color) => color,
        None => return,
    };

    let d = &layout_box.dimensions;
    let border_box = d.border_box();

    // Left border
    list.push(DisplayCommand::SolidColor(
        color,
        Rect {
            x: border_box.x,
            y: border_box.y,
            width: d.border.left,
            height: border_box.height,
        },
    ));
    // Right border
    list.push(DisplayCommand::SolidColor(
        color,
        Rect {
            x: border_box.x + border_box.width - d.border.right,
            y: border_box.y,
            width: d.border.right,
            height: border_box.height,
        },
    ));
    // Top border
    list.push(DisplayCommand::SolidColor(
        color,
        Rect {
            x: border_box.x,
            y: border_box.y,
            width: border_box.width,
            height: d.border.top,
        },
    ));
    // Bottom border
    list.push(DisplayCommand::SolidColor(
        color,
        Rect {
            x: border_box.x,
            y: border_box.y + border_box.height - d.border.bottom,
            width: border_box.width,
            height: d.border.bottom,
        },
    ));
}

pub struct Canvas {
    pub pixels: Vec<Color>,
    pub width: usize,
    pub height: usize,
}

impl Canvas {
    pub fn new(width: usize, height: usize) -> Canvas {
        Canvas {
            pixels: vec![Color::rgb(255, 255, 255); width * height],
            width,
            height,
        }
    }

    fn paint_item(&mut self, item: &DisplayCommand) {
        match *item {
            DisplayCommand::SolidColor(color, rect) => {
                let x0 = rect.x.clamp(0.0, self.width as f32) as usize;
                let y0 = rect.y.clamp(0.0, self.height as f32) as usize;
                let x1 = (rect.x + rect.width).clamp(0.0, self.width as f32) as usize;
                let y1 = (rect.y + rect.height).clamp(0.0, self.height as f32) as usize;

                for y in y0..y1 {
                    for x in x0..x1 {
                        self.pixels[y * self.width + x] = color;
                    }
                }
            }
        }
    }

    /// Binary P6 PPM (`tools/ppm_to_png.py` expects this variant).
    pub fn to_ppm(&self) -> Vec<u8> {
        let header = format!("P6\n{} {}\n255\n", self.width, self.height);
        let mut out = Vec::with_capacity(header.len() + self.pixels.len() * 3);
        out.extend_from_slice(header.as_bytes());
        for pixel in &self.pixels {
            out.push(pixel.r);
            out.push(pixel.g);
            out.push(pixel.b);
        }
        out
    }
}

pub fn paint(layout_root: &LayoutBox, bounds: Rect) -> Canvas {
    let display_list = build_display_list(layout_root);
    let mut canvas = Canvas::new(bounds.width as usize, bounds.height as usize);
    for item in &display_list {
        canvas.paint_item(item);
    }
    canvas
}
