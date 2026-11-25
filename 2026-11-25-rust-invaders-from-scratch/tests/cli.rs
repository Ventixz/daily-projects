//! Runs the real binary headlessly, and checks its PNG output decodes.

use std::process::Command;

fn bin() -> Command {
    Command::new(env!("CARGO_BIN_EXE_starfall"))
}

#[test]
fn headless_run_prints_a_summary() {
    let out = bin()
        .args(["--headless", "--seed", "3", "--ticks", "1200"])
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let text = String::from_utf8_lossy(&out.stdout);
    assert!(text.starts_with("ticks 1200 (20s)  score "), "{text}");
}

#[test]
fn bad_arguments_exit_with_usage() {
    let out = bin().arg("--bogus").output().unwrap();
    assert_eq!(out.status.code(), Some(2));
    assert!(String::from_utf8_lossy(&out.stderr).contains("unknown argument: --bogus"));
    let out = bin().arg("--help").output().unwrap();
    assert!(out.status.success());
    assert!(String::from_utf8_lossy(&out.stdout).contains("USAGE"));
}

#[test]
fn screenshots_are_valid_pngs() {
    let dir = std::env::temp_dir().join(format!("starfall-test-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("shot.png");
    let out = bin()
        .args(["--headless", "--ticks", "600", "--shot"])
        .arg(&path)
        .output()
        .unwrap();
    assert!(
        out.status.success(),
        "{}",
        String::from_utf8_lossy(&out.stderr)
    );
    let png = std::fs::read(&path).unwrap();
    assert!(png.starts_with(b"\x89PNG\r\n\x1a\n"));
    assert!(
        png.len() < 20_000,
        "a frame of pixel art should compress well, got {} bytes",
        png.len()
    );

    // Cross-check the deflate stream with an independent decoder when one is
    // around: Python's zlib must inflate IDAT to exactly 256 * (1 + 224 * 3) bytes.
    let script = r#"
import struct, sys, zlib
d = open(sys.argv[1], 'rb').read(); i = 8; idat = b''
while i < len(d):
    n, t = struct.unpack('>I', d[i:i+4])[0], d[i+4:i+8]
    assert zlib.crc32(d[i+4:i+8+n]) == struct.unpack('>I', d[i+8+n:i+12+n])[0], t
    if t == b'IDAT': idat += d[i+8:i+8+n]
    i += 12 + n
print(len(zlib.decompress(idat)))
"#;
    match Command::new("python3")
        .arg("-c")
        .arg(script)
        .arg(&path)
        .output()
    {
        Ok(o) if o.status.success() => {
            assert_eq!(
                String::from_utf8_lossy(&o.stdout).trim(),
                (256 * (1 + 224 * 3)).to_string()
            );
        }
        Ok(o) => panic!(
            "python rejected the PNG: {}",
            String::from_utf8_lossy(&o.stderr)
        ),
        Err(_) => eprintln!("python3 not found; skipping the independent zlib check"),
    }
    std::fs::remove_dir_all(&dir).ok();
}
