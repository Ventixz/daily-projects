//! Block layout: turns a style tree into a tree of boxes with concrete
//! pixel dimensions, following the CSS2.1 visual formatting model for
//! block-level boxes in normal flow (no floats, no positioning, no inline
//! text layout — inline boxes exist only so anonymous block wrapping has
//! something to wrap, and are left zero-sized, same limitation as the
//! tutorial this follows).

use crate::css::Value::{ColorValue, Keyword, Length};
use crate::css::{Color, Unit::Px};
use crate::style::{Display, StyledNode};

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Rect {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct EdgeSizes {
    pub left: f32,
    pub right: f32,
    pub top: f32,
    pub bottom: f32,
}

#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct Dimensions {
    pub content: Rect,
    pub padding: EdgeSizes,
    pub border: EdgeSizes,
    pub margin: EdgeSizes,
}

impl Rect {
    pub fn expanded_by(&self, edge: EdgeSizes) -> Rect {
        Rect {
            x: self.x - edge.left,
            y: self.y - edge.top,
            width: self.width + edge.left + edge.right,
            height: self.height + edge.top + edge.bottom,
        }
    }
}

impl Dimensions {
    pub fn padding_box(&self) -> Rect {
        self.content.expanded_by(self.padding)
    }
    pub fn border_box(&self) -> Rect {
        self.padding_box().expanded_by(self.border)
    }
    pub fn margin_box(&self) -> Rect {
        self.border_box().expanded_by(self.margin)
    }
}

#[derive(Debug)]
pub enum BoxType<'a> {
    BlockNode(&'a StyledNode<'a>),
    InlineNode(&'a StyledNode<'a>),
    AnonymousBlock,
}

#[derive(Debug)]
pub struct LayoutBox<'a> {
    pub dimensions: Dimensions,
    pub box_type: BoxType<'a>,
    pub children: Vec<LayoutBox<'a>>,
}

impl<'a> LayoutBox<'a> {
    fn new(box_type: BoxType<'a>) -> LayoutBox<'a> {
        LayoutBox {
            dimensions: Dimensions::default(),
            box_type,
            children: Vec::new(),
        }
    }

    fn get_style_node(&self) -> &'a StyledNode<'a> {
        match self.box_type {
            BoxType::BlockNode(node) | BoxType::InlineNode(node) => node,
            BoxType::AnonymousBlock => panic!("Anonymous block box has no style node"),
        }
    }

    fn get_inline_container(&mut self) -> &mut LayoutBox<'a> {
        match self.box_type {
            BoxType::InlineNode(_) | BoxType::AnonymousBlock => self,
            BoxType::BlockNode(_) => {
                match self.children.last() {
                    Some(&LayoutBox {
                        box_type: BoxType::AnonymousBlock,
                        ..
                    }) => {}
                    _ => self.children.push(LayoutBox::new(BoxType::AnonymousBlock)),
                }
                self.children.last_mut().unwrap()
            }
        }
    }
}

pub fn layout_tree<'a>(
    node: &'a StyledNode<'a>,
    mut containing_block: Dimensions,
) -> LayoutBox<'a> {
    containing_block.content.height = 0.0;
    let mut root_box = build_layout_tree(node);
    root_box.layout(containing_block);
    root_box
}

fn build_layout_tree<'a>(style_node: &'a StyledNode<'a>) -> LayoutBox<'a> {
    let mut root = LayoutBox::new(match style_node.display() {
        Display::Block => BoxType::BlockNode(style_node),
        Display::Inline => BoxType::InlineNode(style_node),
        Display::None => panic!("build_layout_tree called with display:none root"),
    });

    for child in &style_node.children {
        match child.display() {
            Display::Block => root.children.push(build_layout_tree(child)),
            Display::Inline => root
                .get_inline_container()
                .children
                .push(build_layout_tree(child)),
            Display::None => {}
        }
    }
    root
}

impl<'a> LayoutBox<'a> {
    fn layout(&mut self, containing_block: Dimensions) {
        match self.box_type {
            BoxType::BlockNode(_) => self.layout_block(containing_block),
            BoxType::InlineNode(_) => {}
            BoxType::AnonymousBlock => self.layout_block(containing_block),
        }
    }

    fn layout_block(&mut self, containing_block: Dimensions) {
        self.calculate_block_width(containing_block);
        self.calculate_block_position(containing_block);
        self.layout_block_children();
        self.calculate_block_height();
    }

    fn calculate_block_width(&mut self, containing_block: Dimensions) {
        let style = match self.box_type {
            BoxType::AnonymousBlock => {
                self.dimensions.content.width = containing_block.content.width;
                return;
            }
            _ => self.get_style_node(),
        };

        let auto = Keyword("auto".to_string());
        let mut width = style.value("width").unwrap_or_else(|| auto.clone());

        let zero = Length(0.0, Px);
        let mut margin_left = style.lookup("margin-left", "margin", &zero);
        let mut margin_right = style.lookup("margin-right", "margin", &zero);

        let border_left = style.lookup("border-left-width", "border-width", &zero);
        let border_right = style.lookup("border-right-width", "border-width", &zero);

        let padding_left = style.lookup("padding-left", "padding", &zero);
        let padding_right = style.lookup("padding-right", "padding", &zero);

        let total: f32 = [
            &margin_left,
            &margin_right,
            &border_left,
            &border_right,
            &padding_left,
            &padding_right,
            &width,
        ]
        .iter()
        .map(|v| v.to_px())
        .sum();

        if width != auto && total > containing_block.content.width {
            if margin_left == auto {
                margin_left = Length(0.0, Px);
            }
            if margin_right == auto {
                margin_right = Length(0.0, Px);
            }
        }

        let underflow = containing_block.content.width - total;

        match (width == auto, margin_left == auto, margin_right == auto) {
            (false, false, false) => {
                margin_right = Length(margin_right.to_px() + underflow, Px);
            }
            (false, false, true) => {
                margin_right = Length(underflow, Px);
            }
            (false, true, false) => {
                margin_left = Length(underflow, Px);
            }
            (true, _, _) => {
                if margin_left == auto {
                    margin_left = Length(0.0, Px);
                }
                if margin_right == auto {
                    margin_right = Length(0.0, Px);
                }
                if underflow >= 0.0 {
                    width = Length(underflow, Px);
                } else {
                    width = Length(0.0, Px);
                    margin_right = Length(margin_right.to_px() + underflow, Px);
                }
            }
            (false, true, true) => {
                margin_left = Length(underflow / 2.0, Px);
                margin_right = Length(underflow / 2.0, Px);
            }
        }

        let d = &mut self.dimensions;
        d.content.width = width.to_px();
        d.padding.left = padding_left.to_px();
        d.padding.right = padding_right.to_px();
        d.border.left = border_left.to_px();
        d.border.right = border_right.to_px();
        d.margin.left = margin_left.to_px();
        d.margin.right = margin_right.to_px();
    }

    fn calculate_block_position(&mut self, containing_block: Dimensions) {
        let zero = Length(0.0, Px);

        let edges = match self.box_type {
            BoxType::AnonymousBlock => None,
            _ => {
                let style = self.get_style_node();
                Some((
                    style.lookup("margin-top", "margin", &zero).to_px(),
                    style.lookup("margin-bottom", "margin", &zero).to_px(),
                    style
                        .lookup("border-top-width", "border-width", &zero)
                        .to_px(),
                    style
                        .lookup("border-bottom-width", "border-width", &zero)
                        .to_px(),
                    style.lookup("padding-top", "padding", &zero).to_px(),
                    style.lookup("padding-bottom", "padding", &zero).to_px(),
                ))
            }
        };

        let d = &mut self.dimensions;
        if let Some((
            margin_top,
            margin_bottom,
            border_top,
            border_bottom,
            padding_top,
            padding_bottom,
        )) = edges
        {
            d.margin.top = margin_top;
            d.margin.bottom = margin_bottom;
            d.border.top = border_top;
            d.border.bottom = border_bottom;
            d.padding.top = padding_top;
            d.padding.bottom = padding_bottom;
        } else {
            d.margin.top = 0.0;
            d.border.top = 0.0;
            d.padding.top = 0.0;
        }

        d.content.x = containing_block.content.x + d.margin.left + d.border.left + d.padding.left;
        d.content.y = containing_block.content.height
            + containing_block.content.y
            + d.margin.top
            + d.border.top
            + d.padding.top;
    }

    fn layout_block_children(&mut self) {
        let d = &mut self.dimensions;
        for child in &mut self.children {
            child.layout(*d);
            d.content.height += child.dimensions.margin_box().height;
        }
    }

    fn calculate_block_height(&mut self) {
        let explicit = match self.box_type {
            BoxType::AnonymousBlock => None,
            _ => self.get_style_node().value("height"),
        };
        if let Some(Length(h, Px)) = explicit {
            self.dimensions.content.height = h;
        }
    }
}

/// Depth-first list of (border box, background color) pairs in paint order,
/// used to build the display list in `painting.rs`.
pub fn background_color(node: &LayoutBox) -> Option<Color> {
    match node.box_type {
        BoxType::BlockNode(style) | BoxType::InlineNode(style) => match style.value("background") {
            Some(ColorValue(c)) => Some(c),
            _ => None,
        },
        BoxType::AnonymousBlock => None,
    }
}
