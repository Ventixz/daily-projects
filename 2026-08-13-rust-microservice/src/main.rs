use std::env;
use std::sync::Arc;

use rust_microservice::server;
use rust_microservice::store::Store;

fn main() {
    let addr = env::args()
        .nth(1)
        .unwrap_or_else(|| "127.0.0.1:7878".to_string());
    let store = Arc::new(Store::new());
    if let Err(e) = server::run(&addr, store) {
        eprintln!("server error: {e}");
        std::process::exit(1);
    }
}
