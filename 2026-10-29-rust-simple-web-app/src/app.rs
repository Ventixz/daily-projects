//! The application: an in-memory notes board with a tiny router.

use crate::http::{html_escape, parse_form, Request, Response};

#[derive(Default)]
pub struct App {
    notes: Vec<String>,
}

impl App {
    pub fn new() -> Self {
        App::default()
    }

    pub fn notes(&self) -> &[String] {
        &self.notes
    }

    pub fn handle(&mut self, req: &Request) -> Response {
        match (req.method.as_str(), req.path.as_str()) {
            ("GET", "/") => Response::html(200, self.render_index()),
            ("POST", "/notes") => {
                let form = parse_form(&String::from_utf8_lossy(&req.body));
                match form.get("text").map(|t| t.trim()).filter(|t| !t.is_empty()) {
                    Some(t) => {
                        self.notes.push(t.chars().take(280).collect());
                        Response::redirect("/")
                    }
                    None => Response::text(400, "note text is required"),
                }
            }
            ("POST", "/notes/delete") => {
                let form = parse_form(&String::from_utf8_lossy(&req.body));
                match form.get("id").and_then(|i| i.parse::<usize>().ok()) {
                    Some(i) if i < self.notes.len() => {
                        self.notes.remove(i);
                        Response::redirect("/")
                    }
                    _ => Response::text(400, "unknown note id"),
                }
            }
            ("GET", "/health") => Response::text(200, "ok"),
            (_, "/") | (_, "/notes") | (_, "/notes/delete") | (_, "/health") => {
                Response::text(405, "method not allowed")
            }
            _ => Response::text(404, "not found"),
        }
    }

    fn render_index(&self) -> String {
        let mut items = String::new();
        for (i, n) in self.notes.iter().enumerate() {
            items.push_str(&format!(
                "<li>{} <form method=\"post\" action=\"/notes/delete\" style=\"display:inline\">\
                 <input type=\"hidden\" name=\"id\" value=\"{}\"><button>delete</button></form></li>\n",
                html_escape(n),
                i
            ));
        }
        format!(
            "<!doctype html><html><head><meta charset=\"utf-8\"><title>Notes</title></head><body>\n\
             <h1>Notes ({})</h1>\n<form method=\"post\" action=\"/notes\">\
             <input name=\"text\" maxlength=\"280\" autofocus><button>add</button></form>\n<ul>\n{}</ul>\n</body></html>",
            self.notes.len(),
            items
        )
    }
}
