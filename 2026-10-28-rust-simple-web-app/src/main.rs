use std::net::TcpListener;
use std::sync::{Arc, Mutex};

use rust_simple_web_app::{server, store::Store};

fn main() -> std::io::Result<()> {
    let addr = std::env::args().nth(1).unwrap_or_else(|| "127.0.0.1:8080".to_string());
    let listener = TcpListener::bind(&addr)?;
    println!("listening on http://{}", listener.local_addr()?);
    server::run(listener, Arc::new(Mutex::new(Store::new())));
    Ok(())
}
