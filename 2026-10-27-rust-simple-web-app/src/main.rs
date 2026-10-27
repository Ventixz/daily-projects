use rust_simple_web_app::{handle, Store};
use std::net::TcpListener;
use std::sync::{Arc, Mutex};
use std::thread;

fn main() -> std::io::Result<()> {
    let addr = std::env::args().nth(1).unwrap_or_else(|| "127.0.0.1:8080".into());
    let listener = TcpListener::bind(&addr)?;
    println!("listening on http://{}", addr);
    let store = Arc::new(Mutex::new(Store::default()));
    for stream in listener.incoming().flatten() {
        let store = Arc::clone(&store);
        thread::spawn(move || {
            let mut stream = stream;
            handle(&mut stream, &store);
        });
    }
    Ok(())
}
