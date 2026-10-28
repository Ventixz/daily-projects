//! In-memory todo store.

#[derive(Debug, Clone, PartialEq)]
pub struct Todo {
    pub id: u32,
    pub title: String,
    pub done: bool,
}

#[derive(Debug, Default)]
pub struct Store {
    todos: Vec<Todo>,
    next_id: u32,
}

impl Store {
    pub fn new() -> Self {
        Store { todos: Vec::new(), next_id: 1 }
    }

    /// Returns `None` for blank titles.
    pub fn add(&mut self, title: &str) -> Option<&Todo> {
        let title = title.trim();
        if title.is_empty() {
            return None;
        }
        let id = self.next_id;
        self.next_id += 1;
        self.todos.push(Todo { id, title: title.to_string(), done: false });
        self.todos.last()
    }

    pub fn list(&self) -> &[Todo] {
        &self.todos
    }

    pub fn get(&self, id: u32) -> Option<&Todo> {
        self.todos.iter().find(|t| t.id == id)
    }

    pub fn toggle(&mut self, id: u32) -> Option<&Todo> {
        let t = self.todos.iter_mut().find(|t| t.id == id)?;
        t.done = !t.done;
        Some(t)
    }

    pub fn delete(&mut self, id: u32) -> bool {
        let before = self.todos.len();
        self.todos.retain(|t| t.id != id);
        self.todos.len() != before
    }
}
