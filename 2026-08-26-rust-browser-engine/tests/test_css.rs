use robinson::css::{parse, Color, Selector, Unit, Value};

#[test]
fn parses_tag_id_class_selector() {
    let sheet = parse("div#main.a.b { color: #ff0000; }".to_string());
    assert_eq!(sheet.rules.len(), 1);
    let Selector::Simple(ref sel) = sheet.rules[0].selectors[0];
    assert_eq!(sel.tag_name, Some("div".to_string()));
    assert_eq!(sel.id, Some("main".to_string()));
    assert_eq!(sel.class, vec!["a".to_string(), "b".to_string()]);
}

#[test]
fn parses_comma_separated_selector_list_sorted_by_specificity() {
    let sheet = parse("p, #id, .cls { display: block; }".to_string());
    let selectors = &sheet.rules[0].selectors;
    assert_eq!(selectors.len(), 3);
    // #id (1,0,0) first, then .cls (0,1,0), then p (0,0,1)
    assert_eq!(selectors[0].specificity(), (1, 0, 0));
    assert_eq!(selectors[1].specificity(), (0, 1, 0));
    assert_eq!(selectors[2].specificity(), (0, 0, 1));
}

#[test]
fn parses_length_and_defaults_unitless_to_px() {
    let sheet = parse("div { width: 12px; margin: 4; }".to_string());
    let decls = &sheet.rules[0].declarations;
    assert_eq!(decls[0].name, "width");
    assert_eq!(decls[0].value, Value::Length(12.0, Unit::Px));
    assert_eq!(decls[1].value, Value::Length(4.0, Unit::Px));
}

#[test]
fn parses_full_and_short_hex_colors() {
    let sheet = parse("a { background: #336699; } b { background: #369; }".to_string());
    assert_eq!(
        sheet.rules[0].declarations[0].value,
        Value::ColorValue(Color::rgb(0x33, 0x66, 0x99))
    );
    assert_eq!(
        sheet.rules[1].declarations[0].value,
        Value::ColorValue(Color::rgb(0x33, 0x66, 0x99))
    );
}

#[test]
fn parses_keyword_value() {
    let sheet = parse("div { display: block; }".to_string());
    assert_eq!(
        sheet.rules[0].declarations[0].value,
        Value::Keyword("block".to_string())
    );
}

#[test]
fn parses_multiple_rules() {
    let sheet = parse("a { color: #fff; } b { color: #000; }".to_string());
    assert_eq!(sheet.rules.len(), 2);
}

#[test]
fn to_px_is_zero_for_non_length_values() {
    assert_eq!(Value::Keyword("auto".to_string()).to_px(), 0.0);
    assert_eq!(Value::ColorValue(Color::rgb(1, 2, 3)).to_px(), 0.0);
}
