//! The only module that knows a DOM exists. Re-renders by replacing `#app`'s
//! `innerHTML` wholesale on every change instead of diffing — for a todo list
//! that's a handful of `<li>`s, rebuilding the string is cheaper than tracking
//! what changed. The tradeoff: every element inside `#app` is destroyed and
//! recreated each render, so listeners are attached once to `#app` itself
//! (which survives) and dispatched by inspecting `event.target()`, rather than
//! attached to children that won't exist after the next keystroke.

use crate::state::{AppState, Filter};
use std::cell::RefCell;
use std::rc::Rc;
use wasm_bindgen::prelude::*;
use wasm_bindgen::JsCast;
use web_sys::{Element, Event, HtmlInputElement, KeyboardEvent, Window};

const STORAGE_KEY: &str = "rust-wasm-todo";

#[wasm_bindgen(start)]
pub fn start() {
    let window = web_sys::window().expect("no global `window`");
    let document = window.document().expect("window has no document");
    let app = document
        .get_element_by_id("app")
        .expect("index.html must contain <div id=\"app\">");

    let mut initial = load(&window).unwrap_or_else(AppState::new);
    initial.set_filter(Filter::from_hash(
        &window.location().hash().unwrap_or_default(),
    ));
    let state = Rc::new(RefCell::new(initial));

    render(&app, &state.borrow());
    wire_clicks(&window, &app, &state);
    wire_new_todo(&window, &app, &state);
    wire_hash_routing(&window, &app, &state);
}

fn wire_clicks(window: &Window, app: &Element, state: &Rc<RefCell<AppState>>) {
    let state = state.clone();
    let app_el = app.clone();
    let window = window.clone();
    let closure = Closure::<dyn FnMut(Event)>::new(move |event: Event| {
        let Some(target) = event.target().and_then(|t| t.dyn_into::<Element>().ok()) else {
            return;
        };
        let id = target
            .get_attribute("data-id")
            .and_then(|s| s.parse::<u32>().ok());
        let changed = if target.class_list().contains("toggle") {
            id.map(|id| state.borrow_mut().toggle(id)).is_some()
        } else if target.class_list().contains("remove") {
            id.map(|id| state.borrow_mut().remove(id)).is_some()
        } else if target.id() == "clear-completed" {
            state.borrow_mut().clear_completed();
            true
        } else {
            false
        };
        if changed {
            persist(&window, &state.borrow());
            render(&app_el, &state.borrow());
        }
    });
    app.add_event_listener_with_callback("click", closure.as_ref().unchecked_ref())
        .expect("failed to attach click listener");
    closure.forget();
}

fn wire_new_todo(window: &Window, app: &Element, state: &Rc<RefCell<AppState>>) {
    let state = state.clone();
    let app_el = app.clone();
    let window = window.clone();
    let closure = Closure::<dyn FnMut(Event)>::new(move |event: Event| {
        let Some(input) = event
            .target()
            .and_then(|t| t.dyn_into::<HtmlInputElement>().ok())
        else {
            return;
        };
        if input.id() != "new-todo" {
            return;
        }
        let Some(key_event) = event.dyn_ref::<KeyboardEvent>() else {
            return;
        };
        if key_event.key() != "Enter" {
            return;
        }
        if state.borrow_mut().add(&input.value()) {
            persist(&window, &state.borrow());
            render(&app_el, &state.borrow());
        }
    });
    app.add_event_listener_with_callback("keydown", closure.as_ref().unchecked_ref())
        .expect("failed to attach keydown listener");
    closure.forget();
}

fn wire_hash_routing(window: &Window, app: &Element, state: &Rc<RefCell<AppState>>) {
    let state = state.clone();
    let app_el = app.clone();
    let window_ref = window.clone();
    let closure = Closure::<dyn FnMut(Event)>::new(move |_event: Event| {
        let hash = window_ref.location().hash().unwrap_or_default();
        state.borrow_mut().set_filter(Filter::from_hash(&hash));
        render(&app_el, &state.borrow());
    });
    window
        .add_event_listener_with_callback("hashchange", closure.as_ref().unchecked_ref())
        .expect("failed to attach hashchange listener");
    closure.forget();
}

fn render(app: &Element, state: &AppState) {
    app.set_inner_html(&state.render());
}

fn load(window: &Window) -> Option<AppState> {
    let storage = window.local_storage().ok()??;
    let raw = storage.get_item(STORAGE_KEY).ok()??;
    Some(AppState::deserialize(&raw))
}

fn persist(window: &Window, state: &AppState) {
    if let Ok(Some(storage)) = window.local_storage() {
        let _ = storage.set_item(STORAGE_KEY, &state.serialize());
    }
}
