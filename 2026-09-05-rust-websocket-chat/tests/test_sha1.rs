use wschat::sha1::sha1;

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

#[test]
fn empty_string() {
    assert_eq!(hex(&sha1(b"")), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

#[test]
fn abc() {
    assert_eq!(hex(&sha1(b"abc")), "a9993e364706816aba3e25717850c26c9cd0d89d");
}

#[test]
fn quick_brown_fox() {
    assert_eq!(
        hex(&sha1(b"The quick brown fox jumps over the lazy dog")),
        "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"
    );
}

#[test]
fn longer_than_one_block() {
    // 64-byte block boundary: one full block of 'a' plus one more byte, so padding has to
    // spill into a second block instead of fitting in the first.
    let input = vec![b'a'; 65];
    assert_eq!(hex(&sha1(&input)), "11655326c708d70319be2610e8a57d9a5b959d3b");
}
