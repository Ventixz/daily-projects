package tracker

import (
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"

	"gotorrent/torrentfile"
)

func testTorrentFile(announce string) *torrentfile.TorrentFile {
	tf := &torrentfile.TorrentFile{
		Announce:    announce,
		PieceLength: 16,
		Length:      100,
		Name:        "demo.txt",
	}
	for i := range tf.InfoHash {
		tf.InfoHash[i] = byte(i)
	}
	return tf
}

func TestBuildAnnounceURLIncludesRequiredParams(t *testing.T) {
	tf := testTorrentFile("http://tracker.example/announce")
	var peerID [20]byte
	copy(peerID[:], "-GT0001-abcdefghijkl")

	raw, err := BuildAnnounceURL(tf, peerID, 6881)
	if err != nil {
		t.Fatal(err)
	}
	u, err := url.Parse(raw)
	if err != nil {
		t.Fatal(err)
	}
	q := u.Query()
	if q.Get("port") != "6881" {
		t.Errorf("port = %q", q.Get("port"))
	}
	if q.Get("left") != "100" {
		t.Errorf("left = %q", q.Get("left"))
	}
	if q.Get("compact") != "1" {
		t.Errorf("compact = %q", q.Get("compact"))
	}
	// info_hash round-trips through URL encoding even though it's raw bytes.
	if got := q.Get("info_hash"); len(got) != 20 {
		t.Errorf("info_hash decoded to %d bytes, want 20", len(got))
	}
	if got := q.Get("info_hash"); got != string(tf.InfoHash[:]) {
		t.Errorf("info_hash did not round-trip through URL encoding")
	}
}

func TestRequestPeersParsesCompactPeerList(t *testing.T) {
	peerBytes := []byte{127, 0, 0, 1, 0x1A, 0xE1} // 127.0.0.1:6881
	body, err := EncodeResponse(1800, peerBytes)
	if err != nil {
		t.Fatal(err)
	}

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.Contains(r.URL.RawQuery, "compact=1") {
			t.Errorf("request missing compact=1: %s", r.URL.RawQuery)
		}
		w.Write(body)
	}))
	defer srv.Close()

	tf := testTorrentFile(srv.URL + "/announce")
	var peerID [20]byte
	peers, err := RequestPeers(tf, peerID, 6881)
	if err != nil {
		t.Fatal(err)
	}
	if len(peers) != 1 {
		t.Fatalf("got %d peers, want 1", len(peers))
	}
	if peers[0].String() != "127.0.0.1:6881" {
		t.Errorf("peer = %s", peers[0])
	}
}

func TestRequestPeersSurfacesFailureReason(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body, _ := EncodeResponse(0, nil)
		// Overwrite with a failure-reason response instead.
		_ = body
		w.Write([]byte("d14:failure reason17:torrent not founde"))
	}))
	defer srv.Close()

	tf := testTorrentFile(srv.URL + "/announce")
	var peerID [20]byte
	_, err := RequestPeers(tf, peerID, 6881)
	if err == nil {
		t.Fatal("expected error for failure-reason response")
	}
	if !strings.Contains(err.Error(), "torrent not found") {
		t.Errorf("error %q did not include tracker's failure reason", err)
	}
}
