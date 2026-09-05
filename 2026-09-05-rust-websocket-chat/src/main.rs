fn main() -> std::io::Result<()> {
    let addr = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "127.0.0.1:9001".to_string());
    println!("wschat listening on {addr} (connect with a WebSocket client, e.g. a browser console: new WebSocket('ws://{addr}'))");
    wschat::server::run(&addr)
}
