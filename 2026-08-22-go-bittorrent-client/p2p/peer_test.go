package p2p

import "testing"

func TestUnmarshalPeers(t *testing.T) {
	// Two peers: 127.0.0.1:6881 and 10.0.0.5:51429.
	raw := []byte{
		127, 0, 0, 1, 0x1A, 0xE1, // 6881 = 0x1AE1
		10, 0, 0, 5, 0xC8, 0xE5, // 51429 = 0xC8E5
	}
	peers, err := UnmarshalPeers(raw)
	if err != nil {
		t.Fatal(err)
	}
	if len(peers) != 2 {
		t.Fatalf("got %d peers, want 2", len(peers))
	}
	if peers[0].IP.String() != "127.0.0.1" || peers[0].Port != 6881 {
		t.Errorf("peers[0] = %+v", peers[0])
	}
	if peers[1].IP.String() != "10.0.0.5" || peers[1].Port != 51429 {
		t.Errorf("peers[1] = %+v", peers[1])
	}
	if got := peers[0].String(); got != "127.0.0.1:6881" {
		t.Errorf("String() = %q", got)
	}
}

func TestUnmarshalPeersRejectsBadLength(t *testing.T) {
	if _, err := UnmarshalPeers([]byte{1, 2, 3}); err == nil {
		t.Error("expected error for a length that isn't a multiple of 6")
	}
}
