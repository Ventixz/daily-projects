use hecto::editor::Editor;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let filename = args.get(1).map(String::as_str);

    match Editor::new(filename) {
        Ok(mut editor) => editor.run(),
        Err(e) => eprintln!("Could not start editor: {e}"),
    }
}
