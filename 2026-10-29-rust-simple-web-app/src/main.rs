use std::net::TcpListener;
use std::sync::{Arc, Mutex};
use std::thread;

use rust_simple_web_app::{app::App, serve_connection};

fn main() -> std::io::Result<()> {
    let addr = std::env::args().nth(1).unwrap_or_else(|| "127.0.0.1:8080".to_string());
    let listener = TcpListener::bind(&addr)?;
    println!("listening on http://{}", addr);
    let app = Arc::new(Mutex::new(App::new()));
    for stream in listener.incoming().flatten() {
        let app = Arc::clone(&app);
        thread::spawn(move || serve_connection(stream, &app));
    }
    Ok(())
}
