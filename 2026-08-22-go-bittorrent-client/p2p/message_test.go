package p2p

import (
	"bytes"
	"testing"
)

func TestHandshakeRoundTrip(t *testing.T) {
	h := &Handshake{}
	copy(h.InfoHash[:], bytes.Repeat([]byte{0xAB}, 20))
	copy(h.PeerID[:], bytes.Repeat([]byte{0xCD}, 20))

	wire := h.Serialize()
	if len(wire) != 68 {
		t.Fatalf("serialized handshake is %d bytes, want 68", len(wire))
	}
	if wire[0] != 19 {
		t.Errorf("pstrlen = %d, want 19", wire[0])
	}

	got, err := ReadHandshake(bytes.NewReader(wire))
	if err != nil {
		t.Fatal(err)
	}
	if got.InfoHash != h.InfoHash || got.PeerID != h.PeerID {
		t.Errorf("handshake did not round-trip: got %+v, want %+v", got, h)
	}
}

func TestMessageRoundTrip(t *testing.T) {
	m := &Message{ID: MsgBitfield, Payload: []byte{0xFF, 0x00, 0xAB}}
	wire := m.Serialize()

	got, err := Read(bytes.NewReader(wire))
	if err != nil {
		t.Fatal(err)
	}
	if got == nil || got.ID != m.ID || !bytes.Equal(got.Payload, m.Payload) {
		t.Errorf("got %+v, want %+v", got, m)
	}
}

func TestKeepAliveRoundTrip(t *testing.T) {
	wire := (*Message)(nil).Serialize()
	if len(wire) != 4 || wire[0] != 0 || wire[1] != 0 || wire[2] != 0 || wire[3] != 0 {
		t.Fatalf("keep-alive wire form = %v, want four zero bytes", wire)
	}
	got, err := Read(bytes.NewReader(wire))
	if err != nil {
		t.Fatal(err)
	}
	if got != nil {
		t.Errorf("Read(keep-alive) = %+v, want nil", got)
	}
}

func TestFormatAndParseRequest(t *testing.T) {
	m := FormatRequest(3, 16384, 16384)
	if m.ID != MsgRequest {
		t.Fatalf("ID = %d, want MsgRequest", m.ID)
	}
	if len(m.Payload) != 12 {
		t.Fatalf("payload length = %d, want 12", len(m.Payload))
	}
}

func TestFormatAndParseRequestRoundTrip(t *testing.T) {
	m := FormatRequest(3, 16384, 8192)
	index, begin, length, err := ParseRequest(m)
	if err != nil {
		t.Fatal(err)
	}
	if index != 3 || begin != 16384 || length != 8192 {
		t.Errorf("got (%d, %d, %d), want (3, 16384, 8192)", index, begin, length)
	}
}

func TestFormatPieceParsesBack(t *testing.T) {
	block := []byte("some block bytes")
	m := FormatPiece(2, 4096, block)
	buf := make([]byte, 4096+len(block))
	n, err := ParsePiece(2, buf, m)
	if err != nil {
		t.Fatal(err)
	}
	if n != len(block) {
		t.Errorf("n = %d, want %d", n, len(block))
	}
	if !bytes.Equal(buf[4096:], block) {
		t.Error("block not at expected offset")
	}
}

func TestFormatAndParseHave(t *testing.T) {
	m := FormatHave(42)
	idx, err := ParseHave(m)
	if err != nil {
		t.Fatal(err)
	}
	if idx != 42 {
		t.Errorf("index = %d, want 42", idx)
	}

	if _, err := ParseHave(&Message{ID: MsgChoke}); err == nil {
		t.Error("expected error parsing HAVE from a non-HAVE message")
	}
}

func TestParsePiece(t *testing.T) {
	buf := make([]byte, 64)
	block := []byte("hello, this is sixteen b")
	payload := make([]byte, 8+len(block))
	payload[3] = 5  // index = 5, big-endian
	payload[7] = 16 // begin = 16, big-endian
	copy(payload[8:], block)
	m := &Message{ID: MsgPiece, Payload: payload}

	n, err := ParsePiece(5, buf, m)
	if err != nil {
		t.Fatal(err)
	}
	if n != len(block) {
		t.Errorf("wrote %d bytes, want %d", n, len(block))
	}
	if !bytes.Equal(buf[16:16+len(block)], block) {
		t.Errorf("block not written at expected offset")
	}
}

func TestParsePieceRejectsWrongIndex(t *testing.T) {
	payload := make([]byte, 8)
	payload[3] = 7 // index = 7
	m := &Message{ID: MsgPiece, Payload: payload}
	if _, err := ParsePiece(5, make([]byte, 8), m); err == nil {
		t.Error("expected error for mismatched piece index")
	}
}

func TestParsePieceRejectsOverflow(t *testing.T) {
	// begin=0, a block bigger than the destination buffer.
	block := make([]byte, 20)
	payload := make([]byte, 8+len(block))
	copy(payload[8:], block)
	m := &Message{ID: MsgPiece, Payload: payload}
	if _, err := ParsePiece(0, make([]byte, 10), m); err == nil {
		t.Error("expected error when block overflows destination buffer")
	}
}

func TestBitfieldHasAndSetPiece(t *testing.T) {
	bf := make(Bitfield, 2) // 16 bits
	if bf.HasPiece(3) {
		t.Fatal("fresh bitfield should have no pieces set")
	}
	bf.SetPiece(3)
	bf.SetPiece(15)
	if !bf.HasPiece(3) || !bf.HasPiece(15) {
		t.Error("SetPiece did not take effect")
	}
	if bf.HasPiece(4) || bf.HasPiece(0) {
		t.Error("SetPiece affected the wrong bit")
	}
	if bf.HasPiece(999) {
		t.Error("HasPiece on an out-of-range index should be false, not panic")
	}
}
