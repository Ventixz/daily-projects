//! A minimal recursive-descent HTML parser producing a `dom::Node` tree.
//!
//! Handles: nested elements, attributes (quoted or bare), text nodes, void
//! elements (`<br>`, `<img>`, ...) without a closing tag, and a permissive
//! fallback that treats an unexpected closing tag as closing everything up
//! to the matching ancestor (or is ignored if there is no ancestor to close).
//! It does not handle comments, CDATA, script/style raw-text parsing, or
//! entity references beyond what real HTML requires.

use crate::dom::{self, AttrMap, Node};

pub fn parse(source: String) -> Node {
    let mut nodes = Parser {
        pos: 0,
        input: source,
    }
    .parse_nodes();

    // If the document doesn't have a single root element, wrap the
    // top-level nodes in an <html> element so callers always get one root.
    if nodes.len() == 1 {
        nodes.swap_remove(0)
    } else {
        Node::elem("html".to_string(), AttrMap::new(), nodes)
    }
}

struct Parser {
    pos: usize,
    input: String,
}

impl Parser {
    fn next_char(&self) -> char {
        self.input[self.pos..].chars().next().unwrap()
    }

    fn starts_with(&self, s: &str) -> bool {
        self.input[self.pos..].starts_with(s)
    }

    fn eof(&self) -> bool {
        self.pos >= self.input.len()
    }

    fn consume_char(&mut self) -> char {
        let mut iter = self.input[self.pos..].char_indices();
        let (_, cur_char) = iter.next().unwrap();
        let (next_pos, _) = iter.next().unwrap_or((1, ' '));
        self.pos += next_pos;
        cur_char
    }

    fn consume_while<F>(&mut self, test: F) -> String
    where
        F: Fn(char) -> bool,
    {
        let mut result = String::new();
        while !self.eof() && test(self.next_char()) {
            result.push(self.consume_char());
        }
        result
    }

    fn consume_whitespace(&mut self) {
        self.consume_while(char::is_whitespace);
    }

    fn parse_tag_name(&mut self) -> String {
        self.consume_while(|c| c.is_ascii_alphanumeric())
    }

    /// Parses a sequence of sibling nodes, stopping at EOF or a closing tag.
    fn parse_nodes(&mut self) -> Vec<Node> {
        let mut nodes = Vec::new();
        loop {
            self.consume_whitespace();
            if self.eof() || self.starts_with("</") {
                break;
            }
            if self.starts_with("<!--") {
                self.consume_comment();
                continue;
            }
            nodes.push(self.parse_node());
        }
        nodes
    }

    fn consume_comment(&mut self) {
        // Consume "<!--"
        for _ in 0..4 {
            self.consume_char();
        }
        while !self.eof() && !self.starts_with("-->") {
            self.consume_char();
        }
        if self.starts_with("-->") {
            for _ in 0..3 {
                self.consume_char();
            }
        }
    }

    fn parse_node(&mut self) -> Node {
        if self.starts_with("<") {
            self.parse_element()
        } else {
            self.parse_text()
        }
    }

    fn parse_text(&mut self) -> Node {
        Node::text(self.consume_while(|c| c != '<'))
    }

    fn parse_element(&mut self) -> Node {
        assert_eq!(self.consume_char(), '<');
        let tag_name = self.parse_tag_name();
        let attrs = self.parse_attributes();
        self.consume_whitespace();

        // Self-closing syntax: <br/> or <img />
        if self.starts_with("/>") {
            self.consume_char();
            self.consume_char();
            return Node::elem(tag_name, attrs, Vec::new());
        }

        assert_eq!(self.consume_char(), '>');

        if dom::is_void_element(&tag_name) {
            return Node::elem(tag_name, attrs, Vec::new());
        }

        let children = self.parse_nodes();

        // Consume the closing tag if it matches; otherwise leave it for an
        // ancestor to consume (permissive recovery for mismatched markup).
        if self.starts_with("</") {
            let save = self.pos;
            self.consume_char();
            self.consume_char();
            let close_name = self.parse_tag_name();
            self.consume_whitespace();
            if close_name == tag_name {
                if self.starts_with(">") {
                    self.consume_char();
                }
            } else {
                self.pos = save;
            }
        }

        Node::elem(tag_name, attrs, children)
    }

    fn parse_attributes(&mut self) -> AttrMap {
        let mut attributes = AttrMap::new();
        loop {
            self.consume_whitespace();
            if self.starts_with(">") || self.starts_with("/>") || self.eof() {
                break;
            }
            let (name, value) = self.parse_attribute();
            if !name.is_empty() {
                attributes.insert(name, value);
            }
        }
        attributes
    }

    fn parse_attribute(&mut self) -> (String, String) {
        let name = self.consume_while(|c| c != '=' && c != '>' && c != '/' && !c.is_whitespace());
        self.consume_whitespace();
        if self.starts_with("=") {
            self.consume_char();
            self.consume_whitespace();
            let value = self.parse_attribute_value();
            (name, value)
        } else {
            (name, String::new())
        }
    }

    fn parse_attribute_value(&mut self) -> String {
        if self.starts_with("\"") || self.starts_with("'") {
            let open_quote = self.consume_char();
            let value = self.consume_while(|c| c != open_quote);
            if !self.eof() {
                self.consume_char();
            }
            value
        } else {
            self.consume_while(|c| !c.is_whitespace() && c != '>')
        }
    }
}
