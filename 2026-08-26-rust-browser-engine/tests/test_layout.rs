use robinson::css::parse as parse_css;
use robinson::html_parser::parse as parse_html;
use robinson::layout::{layout_tree, Dimensions};
use robinson::style::style_tree;

fn viewport(width: f32, height: f32) -> Dimensions {
    let mut d = Dimensions::default();
    d.content.width = width;
    d.content.height = height;
    d
}

#[test]
fn auto_width_fills_containing_block() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css("div { display: block; }".to_string());
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(800.0, 600.0));
    assert_eq!(root.dimensions.content.width, 800.0);
}

#[test]
fn explicit_width_and_centering_margin_auto() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css("div { display: block; width: 200px; margin: auto; }".to_string());
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(800.0, 600.0));
    assert_eq!(root.dimensions.content.width, 200.0);
    // (800 - 200) / 2 = 300 on each side.
    assert_eq!(root.dimensions.margin.left, 300.0);
    assert_eq!(root.dimensions.margin.right, 300.0);
}

#[test]
fn padding_and_border_expand_boxes_without_changing_content_width() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css(
        "div { display: block; width: 100px; padding: 10px; border-width: 2px; }".to_string(),
    );
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(800.0, 600.0));
    assert_eq!(root.dimensions.content.width, 100.0);
    assert_eq!(
        root.dimensions.border_box().width,
        100.0 + 2.0 * 10.0 + 2.0 * 2.0
    );
}

#[test]
fn children_stack_vertically_and_parent_height_sums_them() {
    let dom = parse_html("<div><p>a</p><p>b</p></div>".to_string());
    let css = parse_css(
        "div { display: block; } p { display: block; height: 30px; margin: 5px; }".to_string(),
    );
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(800.0, 0.0));

    assert_eq!(root.children.len(), 2);
    let first = &root.children[0];
    let second = &root.children[1];

    assert_eq!(first.dimensions.content.y, 5.0); // margin-top of first child
    assert_eq!(first.dimensions.content.height, 30.0);
    // second child starts after first child's margin box (30 + 5+5 margin)
    assert_eq!(
        second.dimensions.content.y,
        first.dimensions.margin_box().height + 5.0
    );

    // parent's auto height is the sum of children's margin boxes.
    let expected_child_total =
        first.dimensions.margin_box().height + second.dimensions.margin_box().height;
    assert_eq!(root.dimensions.content.height, expected_child_total);
}

#[test]
fn explicit_height_overrides_computed_height() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css("div { display: block; height: 42px; }".to_string());
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(800.0, 0.0));
    assert_eq!(root.dimensions.content.height, 42.0);
}

#[test]
fn width_wider_than_container_zeroes_auto_margins() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css("div { display: block; width: 1000px; margin: auto; }".to_string());
    let styled = style_tree(&dom, &css);
    let root = layout_tree(&styled, viewport(800.0, 600.0));
    assert_eq!(root.dimensions.margin.left, 0.0);
}
