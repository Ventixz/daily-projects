//! Routing and rendering. `handle` is a pure function of (store, request).

use crate::http::{parse_form, Request, Response};
use crate::store::{Store, Todo};

pub fn escape_html(s: &str) -> String {
    s.chars().fold(String::with_capacity(s.len()), |mut o, c| {
        match c {
            '&' => o.push_str("&amp;"),
            '<' => o.push_str("&lt;"),
            '>' => o.push_str("&gt;"),
            '"' => o.push_str("&quot;"),
            '\'' => o.push_str("&#39;"),
            c => o.push(c),
        }
        o
    })
}

pub fn escape_json(s: &str) -> String {
    let mut o = String::with_capacity(s.len());
    for c in s.chars() {
        match c {
            '"' => o.push_str("\\\""),
            '\\' => o.push_str("\\\\"),
            '\n' => o.push_str("\\n"),
            '\r' => o.push_str("\\r"),
            '\t' => o.push_str("\\t"),
            c if (c as u32) < 0x20 => o.push_str(&format!("\\u{:04x}", c as u32)),
            c => o.push(c),
        }
    }
    o
}

fn todo_json(t: &Todo) -> String {
    format!(r#"{{"id":{},"title":"{}","done":{}}}"#, t.id, escape_json(&t.title), t.done)
}

fn render_page(store: &Store) -> String {
    let mut items = String::new();
    for t in store.list() {
        let class = if t.done { " class=\"done\"" } else { "" };
        items.push_str(&format!(
            "<li{class}><form method=\"post\" action=\"/todos/{id}/toggle\"><button>{mark}</button></form> \
             <span>{title}</span> \
             <form method=\"post\" action=\"/todos/{id}/delete\"><button>x</button></form></li>\n",
            class = class,
            id = t.id,
            mark = if t.done { "&#9745;" } else { "&#9744;" },
            title = escape_html(&t.title),
        ));
    }
    format!(
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Todos</title>\
         <style>.done span{{text-decoration:line-through}}form{{display:inline}}</style></head><body>\
         <h1>Todos</h1><form method=\"post\" action=\"/todos\"><input name=\"title\" autofocus> \
         <button>Add</button></form><ul>\n{items}</ul></body></html>"
    )
}

/// Parse `/todos/<id>[/<action>]` into (id, action).
fn todo_path(path: &str) -> Option<(u32, &str)> {
    let rest = path.strip_prefix("/todos/")?;
    let (id, action) = rest.split_once('/').unwrap_or((rest, ""));
    Some((id.parse().ok()?, action))
}

fn wants_json(req: &Request) -> bool {
    req.headers.get("accept").map_or(false, |a| a.contains("application/json"))
}

fn error(req: &Request, status: u16, msg: &str) -> Response {
    if wants_json(req) {
        Response::json(status, format!(r#"{{"error":"{}"}}"#, escape_json(msg)))
    } else {
        Response::html(status, format!("<h1>{} {}</h1>", status, escape_html(msg)))
    }
}

pub fn handle(store: &mut Store, req: &Request) -> Response {
    let json = wants_json(req);
    match (req.method.as_str(), req.path.as_str()) {
        ("GET", "/") => Response::html(200, render_page(store)),
        ("GET", "/todos") => {
            let items: Vec<String> = store.list().iter().map(todo_json).collect();
            Response::json(200, format!("[{}]", items.join(",")))
        }
        ("POST", "/todos") => {
            let form = parse_form(&String::from_utf8_lossy(&req.body));
            match store.add(form.get("title").map_or("", String::as_str)) {
                None => error(req, 400, "title must not be blank"),
                Some(t) if json => Response::json(201, todo_json(t)),
                Some(_) => Response::redirect("/"),
            }
        }
        (_, "/") | (_, "/todos") => error(req, 405, "method not allowed"),
        (method, path) => match todo_path(path) {
            None => error(req, 404, "not found"),
            Some((id, action)) => match (method, action) {
                ("GET", "") => match store.get(id) {
                    Some(t) => Response::json(200, todo_json(t)),
                    None => error(req, 404, "no such todo"),
                },
                ("POST", "toggle") => match store.toggle(id) {
                    Some(t) if json => Response::json(200, todo_json(t)),
                    Some(_) => Response::redirect("/"),
                    None => error(req, 404, "no such todo"),
                },
                ("POST", "delete") | ("DELETE", "") => {
                    if !store.delete(id) {
                        error(req, 404, "no such todo")
                    } else if json || method == "DELETE" {
                        Response::json(200, r#"{"deleted":true}"#)
                    } else {
                        Response::redirect("/")
                    }
                }
                ("GET", "toggle") | ("GET", "delete") => error(req, 405, "method not allowed"),
                _ => error(req, 404, "not found"),
            },
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    fn req(method: &str, path: &str, body: &str, json: bool) -> Request {
        let mut headers = HashMap::new();
        if json {
            headers.insert("accept".to_string(), "application/json".to_string());
        }
        Request { method: method.into(), path: path.into(), query: HashMap::new(), headers, body: body.as_bytes().to_vec() }
    }

    #[test]
    fn create_list_toggle_delete() {
        let mut s = Store::new();
        let r = handle(&mut s, &req("POST", "/todos", "title=buy+milk", true));
        assert_eq!((r.status, r.body.as_str()), (201, r#"{"id":1,"title":"buy milk","done":false}"#));
        assert_eq!(handle(&mut s, &req("GET", "/todos", "", true)).body, r#"[{"id":1,"title":"buy milk","done":false}]"#);
        assert!(handle(&mut s, &req("POST", "/todos/1/toggle", "", true)).body.contains(r#""done":true"#));
        assert_eq!(handle(&mut s, &req("DELETE", "/todos/1", "", true)).status, 200);
        assert_eq!(handle(&mut s, &req("GET", "/todos/1", "", true)).status, 404);
    }

    #[test]
    fn form_post_redirects() {
        let mut s = Store::new();
        let r = handle(&mut s, &req("POST", "/todos", "title=x", false));
        assert_eq!(r.status, 303);
        assert_eq!(r.extra_headers[0], ("Location".to_string(), "/".to_string()));
    }

    #[test]
    fn blank_title_is_400() {
        let mut s = Store::new();
        assert_eq!(handle(&mut s, &req("POST", "/todos", "title=+++", false)).status, 400);
        assert_eq!(handle(&mut s, &req("POST", "/todos", "", true)).status, 400);
    }

    #[test]
    fn html_page_escapes_titles() {
        let mut s = Store::new();
        s.add("<script>alert(1)</script>");
        let page = handle(&mut s, &req("GET", "/", "", false)).body;
        assert!(!page.contains("<script>alert"));
        assert!(page.contains("&lt;script&gt;"));
    }

    #[test]
    fn json_escapes_quotes_and_controls() {
        assert_eq!(escape_json("a\"b\\c\n\u{1}"), "a\\\"b\\\\c\\n\\u0001");
    }

    #[test]
    fn routing_errors() {
        let mut s = Store::new();
        assert_eq!(handle(&mut s, &req("GET", "/nope", "", false)).status, 404);
        assert_eq!(handle(&mut s, &req("PUT", "/todos", "", false)).status, 405);
        assert_eq!(handle(&mut s, &req("GET", "/todos/abc", "", false)).status, 404);
        assert_eq!(handle(&mut s, &req("POST", "/todos/9/toggle", "", false)).status, 404);
        assert_eq!(handle(&mut s, &req("GET", "/todos/1/delete", "", false)).status, 405);
    }
}
