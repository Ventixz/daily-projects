use std::collections::{HashMap, HashSet};
use std::fmt;

pub type AttrMap = HashMap<String, String>;

#[derive(Debug, PartialEq, Clone)]
pub enum NodeType {
    Text(String),
    Element(ElementData),
}

#[derive(Debug, PartialEq, Clone)]
pub struct ElementData {
    pub tag_name: String,
    pub attributes: AttrMap,
}

#[derive(Debug, PartialEq, Clone)]
pub struct Node {
    pub children: Vec<Node>,
    pub node_type: NodeType,
}

impl Node {
    pub fn text(data: String) -> Node {
        Node {
            children: Vec::new(),
            node_type: NodeType::Text(data),
        }
    }

    pub fn elem(name: String, attrs: AttrMap, children: Vec<Node>) -> Node {
        Node {
            children,
            node_type: NodeType::Element(ElementData {
                tag_name: name,
                attributes: attrs,
            }),
        }
    }
}

impl ElementData {
    pub fn id(&self) -> Option<&String> {
        self.attributes.get("id")
    }

    pub fn classes(&self) -> HashSet<&str> {
        match self.attributes.get("class") {
            Some(classlist) => classlist.split(' ').filter(|s| !s.is_empty()).collect(),
            None => HashSet::new(),
        }
    }
}

impl fmt::Display for Node {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        pretty_print(self, 0, f)
    }
}

fn pretty_print(node: &Node, indent: usize, f: &mut fmt::Formatter) -> fmt::Result {
    let pad = "  ".repeat(indent);
    match &node.node_type {
        NodeType::Text(t) => writeln!(f, "{}\"{}\"", pad, t.trim())?,
        NodeType::Element(e) => {
            writeln!(f, "{}<{}>", pad, e.tag_name)?;
            for child in &node.children {
                pretty_print(child, indent + 1, f)?;
            }
        }
    }
    Ok(())
}

/// Void elements that never have children and are not explicitly closed in
/// our minimal HTML subset.
pub fn is_void_element(tag: &str) -> bool {
    matches!(
        tag,
        "area"
            | "base"
            | "br"
            | "col"
            | "embed"
            | "hr"
            | "img"
            | "input"
            | "link"
            | "meta"
            | "param"
            | "source"
            | "track"
            | "wbr"
    )
}
