package torrentfile

import (
	"bytes"
	"crypto/sha1"
	"strings"
	"testing"

	"gotorrent/bencode"
)

// buildTorrentBytes bencodes a minimal single-file torrent dict by hand,
// mirroring what a real .torrent file on disk contains.
func buildTorrentBytes(t *testing.T, announce, name string, pieceLength, length int, pieces string) []byte {
	t.Helper()
	info := map[string]interface{}{
		"piece length": int64(pieceLength),
		"length":       int64(length),
		"name":         name,
		"pieces":       pieces,
	}
	top := map[string]interface{}{
		"announce": announce,
		"info":     info,
	}
	var buf bytes.Buffer
	if err := bencode.Encode(&buf, top); err != nil {
		t.Fatalf("encoding fixture: %v", err)
	}
	return buf.Bytes()
}

func TestParseSingleFileTorrent(t *testing.T) {
	piece0 := sha1.Sum([]byte("piece-zero-bytes"))
	piece1 := sha1.Sum([]byte("piece-one-bytes-"))
	pieces := string(piece0[:]) + string(piece1[:])

	raw := buildTorrentBytes(t, "http://tracker.example/announce", "demo.txt", 16, 32, pieces)

	tf, err := Parse(bytes.NewReader(raw))
	if err != nil {
		t.Fatalf("Parse: %v", err)
	}

	if tf.Announce != "http://tracker.example/announce" {
		t.Errorf("Announce = %q", tf.Announce)
	}
	if tf.Name != "demo.txt" {
		t.Errorf("Name = %q", tf.Name)
	}
	if tf.PieceLength != 16 || tf.Length != 32 {
		t.Errorf("PieceLength=%d Length=%d", tf.PieceLength, tf.Length)
	}
	if len(tf.PieceHashes) != 2 {
		t.Fatalf("got %d piece hashes, want 2", len(tf.PieceHashes))
	}
	if tf.PieceHashes[0] != piece0 || tf.PieceHashes[1] != piece1 {
		t.Errorf("piece hashes did not round-trip")
	}
}

func TestInfoHashMatchesManualEncoding(t *testing.T) {
	// The info_hash must be SHA-1 of exactly the bencoded "info" dict,
	// not of the whole file and not of some re-serialization that adds
	// or reorders fields. Compute it independently here and compare.
	piece0 := sha1.Sum([]byte("x"))
	raw := buildTorrentBytes(t, "http://t/announce", "f", 8, 8, string(piece0[:]))

	tf, err := Parse(bytes.NewReader(raw))
	if err != nil {
		t.Fatal(err)
	}

	wantInfo := map[string]interface{}{
		"length":       int64(8),
		"name":         "f",
		"piece length": int64(8),
		"pieces":       string(piece0[:]),
	}
	var buf bytes.Buffer
	if err := bencode.Encode(&buf, wantInfo); err != nil {
		t.Fatal(err)
	}
	want := sha1.Sum(buf.Bytes())

	if tf.InfoHash != want {
		t.Errorf("InfoHash = %x, want %x", tf.InfoHash, want)
	}
}

func TestParseRejectsMultiFileTorrent(t *testing.T) {
	info := map[string]interface{}{
		"piece length": int64(16),
		"name":         "dir",
		"pieces":       strings.Repeat("x", 20),
		"files": []interface{}{
			map[string]interface{}{"length": int64(4), "path": []interface{}{"a.txt"}},
		},
	}
	top := map[string]interface{}{"announce": "http://t/announce", "info": info}
	var buf bytes.Buffer
	bencode.Encode(&buf, top)

	if _, err := Parse(bytes.NewReader(buf.Bytes())); err == nil {
		t.Error("expected error for multi-file torrent, got nil")
	}
}

func TestParseRejectsMalformedPieces(t *testing.T) {
	raw := buildTorrentBytes(t, "http://t/announce", "f", 8, 8, "too-short")
	if _, err := Parse(bytes.NewReader(raw)); err == nil {
		t.Error("expected error for pieces length not a multiple of 20, got nil")
	}
}
