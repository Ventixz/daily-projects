use robinson::css::{parse as parse_css, Color, Value};
use robinson::html_parser::parse as parse_html;
use robinson::style::{style_tree, Display};

#[test]
fn matches_by_tag_name() {
    let dom = parse_html("<div>x</div>".to_string());
    let css = parse_css("div { display: block; }".to_string());
    let tree = style_tree(&dom, &css);
    assert_eq!(tree.display(), Display::Block);
}

#[test]
fn unmatched_node_has_no_specified_values() {
    let dom = parse_html("<span>x</span>".to_string());
    let css = parse_css("div { display: block; }".to_string());
    let tree = style_tree(&dom, &css);
    // No rule matches <span>, so it falls back to the inline default.
    assert_eq!(tree.display(), Display::Inline);
}

#[test]
fn higher_specificity_wins_regardless_of_source_order() {
    let dom = parse_html(r#"<div id="x" class="c">t</div>"#.to_string());
    // Tag selector declared last, but #id has higher specificity and must win.
    let css = parse_css("#x { background: #ff0000; } div { background: #00ff00; }".to_string());
    let tree = style_tree(&dom, &css);
    assert_eq!(
        tree.value("background"),
        Some(Value::ColorValue(Color::rgb(0xff, 0, 0)))
    );
}

#[test]
fn equal_specificity_ties_broken_by_source_order() {
    let dom = parse_html("<div>t</div>".to_string());
    let css = parse_css("div { background: #111111; } div { background: #222222; }".to_string());
    let tree = style_tree(&dom, &css);
    assert_eq!(
        tree.value("background"),
        Some(Value::ColorValue(Color::rgb(0x22, 0x22, 0x22)))
    );
}

#[test]
fn class_selector_matches_any_of_multiple_classes() {
    let dom = parse_html(r#"<div class="a b c">t</div>"#.to_string());
    let css = parse_css(".b { display: block; }".to_string());
    let tree = style_tree(&dom, &css);
    assert_eq!(tree.display(), Display::Block);
}

#[test]
fn display_none_is_recognized() {
    let dom = parse_html("<div>t</div>".to_string());
    let css = parse_css("div { display: none; }".to_string());
    let tree = style_tree(&dom, &css);
    assert_eq!(tree.display(), Display::None);
}

#[test]
fn lookup_falls_back_then_to_default() {
    let dom = parse_html("<div>t</div>".to_string());
    let css = parse_css("div { margin: 5px; }".to_string());
    let tree = style_tree(&dom, &css);
    let zero = Value::Length(0.0, robinson::css::Unit::Px);
    // No margin-left, falls back to shorthand `margin`.
    assert_eq!(
        tree.lookup("margin-left", "margin", &zero),
        Value::Length(5.0, robinson::css::Unit::Px)
    );
}

#[test]
fn style_tree_recurses_into_children() {
    let dom = parse_html("<div><p>t</p></div>".to_string());
    let css = parse_css("p { display: block; }".to_string());
    let tree = style_tree(&dom, &css);
    assert_eq!(tree.children.len(), 1);
    assert_eq!(tree.children[0].display(), Display::Block);
}
