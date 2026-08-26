use robinson::dom::NodeType;
use robinson::html_parser::parse;

fn elem_tag(node: &robinson::dom::Node) -> &str {
    match &node.node_type {
        NodeType::Element(e) => &e.tag_name,
        NodeType::Text(_) => panic!("expected element, got text"),
    }
}

#[test]
fn parses_single_root_element() {
    let root = parse("<div>hello</div>".to_string());
    assert_eq!(elem_tag(&root), "div");
    assert_eq!(root.children.len(), 1);
    match &root.children[0].node_type {
        NodeType::Text(t) => assert_eq!(t, "hello"),
        _ => panic!("expected text child"),
    }
}

#[test]
fn parses_nested_elements() {
    let root = parse("<div><p>a</p><p>b</p></div>".to_string());
    assert_eq!(elem_tag(&root), "div");
    assert_eq!(root.children.len(), 2);
    assert_eq!(elem_tag(&root.children[0]), "p");
    assert_eq!(elem_tag(&root.children[1]), "p");
}

#[test]
fn wraps_multiple_top_level_nodes_in_html() {
    let root = parse("<p>one</p><p>two</p>".to_string());
    assert_eq!(elem_tag(&root), "html");
    assert_eq!(root.children.len(), 2);
}

#[test]
fn parses_quoted_and_bare_attributes() {
    let root = parse(r#"<div id="main" class='a b' data-x=1>x</div>"#.to_string());
    match &root.node_type {
        NodeType::Element(e) => {
            assert_eq!(e.id(), Some(&"main".to_string()));
            let classes = e.classes();
            assert!(classes.contains("a"));
            assert!(classes.contains("b"));
            assert_eq!(e.attributes.get("data-x"), Some(&"1".to_string()));
        }
        _ => panic!("expected element"),
    }
}

#[test]
fn void_elements_have_no_closing_tag() {
    let root = parse("<div>before<br>after<img src=\"x.png\">tail</div>".to_string());
    // before, br, after, img, tail
    assert_eq!(root.children.len(), 5);
    assert_eq!(elem_tag(&root.children[1]), "br");
    assert!(root.children[1].children.is_empty());
    assert_eq!(elem_tag(&root.children[3]), "img");
}

#[test]
fn self_closing_slash_syntax() {
    let root = parse("<div><hr/></div>".to_string());
    assert_eq!(root.children.len(), 1);
    assert_eq!(elem_tag(&root.children[0]), "hr");
    assert!(root.children[0].children.is_empty());
}

#[test]
fn comments_are_skipped() {
    let root = parse("<div><!-- a comment --><p>x</p></div>".to_string());
    assert_eq!(root.children.len(), 1);
    assert_eq!(elem_tag(&root.children[0]), "p");
}

#[test]
fn empty_document_produces_empty_html_root() {
    let root = parse("   ".to_string());
    assert_eq!(elem_tag(&root), "html");
    assert!(root.children.is_empty());
}
