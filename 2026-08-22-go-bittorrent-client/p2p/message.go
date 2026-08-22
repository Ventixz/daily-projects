// Package p2p implements the BitTorrent peer wire protocol: the handshake,
// the length-prefixed message framing, and the piece bitfield each peer
// advertises.
package p2p

import (
	"encoding/binary"
	"fmt"
	"io"
)

// MessageID identifies a peer wire protocol message type.
type MessageID uint8

const (
	MsgChoke         MessageID = 0
	MsgUnchoke       MessageID = 1
	MsgInterested    MessageID = 2
	MsgNotInterested MessageID = 3
	MsgHave          MessageID = 4
	MsgBitfield      MessageID = 5
	MsgRequest       MessageID = 6
	MsgPiece         MessageID = 7
	MsgCancel        MessageID = 8
)

// Message is a single peer wire protocol message: a 4-byte big-endian
// length prefix (covering ID+Payload), a 1-byte ID, and a payload. A
// zero-length message (nil Message from Read) is the keep-alive.
type Message struct {
	ID      MessageID
	Payload []byte
}

// Serialize encodes m to wire format, including its length prefix.
func (m *Message) Serialize() []byte {
	if m == nil {
		return make([]byte, 4) // keep-alive: length 0, no ID, no payload
	}
	length := uint32(len(m.Payload) + 1) // +1 for the ID byte
	buf := make([]byte, 4+length)
	binary.BigEndian.PutUint32(buf[0:4], length)
	buf[4] = byte(m.ID)
	copy(buf[5:], m.Payload)
	return buf
}

// Read parses a single message (or keep-alive, returned as a nil *Message)
// from r.
func Read(r io.Reader) (*Message, error) {
	lenBuf := make([]byte, 4)
	if _, err := io.ReadFull(r, lenBuf); err != nil {
		return nil, err
	}
	length := binary.BigEndian.Uint32(lenBuf)
	if length == 0 {
		return nil, nil // keep-alive
	}

	buf := make([]byte, length)
	if _, err := io.ReadFull(r, buf); err != nil {
		return nil, err
	}

	return &Message{ID: MessageID(buf[0]), Payload: buf[1:]}, nil
}

// FormatRequest builds a "request" message asking for a block of length
// bytes, starting at byte offset begin within piece index.
func FormatRequest(index, begin, length int) *Message {
	payload := make([]byte, 12)
	binary.BigEndian.PutUint32(payload[0:4], uint32(index))
	binary.BigEndian.PutUint32(payload[4:8], uint32(begin))
	binary.BigEndian.PutUint32(payload[8:12], uint32(length))
	return &Message{ID: MsgRequest, Payload: payload}
}

// ParseRequest extracts (index, begin, length) from a "request" message.
// Used by a peer's serving side -- this client only issues requests, but a
// test fake-seeder needs to decode them.
func ParseRequest(m *Message) (index, begin, length int, err error) {
	if m.ID != MsgRequest {
		return 0, 0, 0, fmt.Errorf("p2p: expected REQUEST (id %d), got id %d", MsgRequest, m.ID)
	}
	if len(m.Payload) != 12 {
		return 0, 0, 0, fmt.Errorf("p2p: REQUEST payload has length %d, want 12", len(m.Payload))
	}
	index = int(binary.BigEndian.Uint32(m.Payload[0:4]))
	begin = int(binary.BigEndian.Uint32(m.Payload[4:8]))
	length = int(binary.BigEndian.Uint32(m.Payload[8:12]))
	return index, begin, length, nil
}

// FormatPiece builds a "piece" message carrying block as the bytes for
// piece index starting at byte offset begin.
func FormatPiece(index, begin int, block []byte) *Message {
	payload := make([]byte, 8+len(block))
	binary.BigEndian.PutUint32(payload[0:4], uint32(index))
	binary.BigEndian.PutUint32(payload[4:8], uint32(begin))
	copy(payload[8:], block)
	return &Message{ID: MsgPiece, Payload: payload}
}

// FormatHave builds a "have" message announcing that we now hold piece index.
func FormatHave(index int) *Message {
	payload := make([]byte, 4)
	binary.BigEndian.PutUint32(payload, uint32(index))
	return &Message{ID: MsgHave, Payload: payload}
}

// ParseHave extracts the piece index from a "have" message.
func ParseHave(m *Message) (int, error) {
	if m.ID != MsgHave {
		return 0, fmt.Errorf("p2p: expected HAVE (id %d), got id %d", MsgHave, m.ID)
	}
	if len(m.Payload) != 4 {
		return 0, fmt.Errorf("p2p: HAVE payload has length %d, want 4", len(m.Payload))
	}
	return int(binary.BigEndian.Uint32(m.Payload)), nil
}

// ParsePiece copies a "piece" message's block into buf at the offset the
// message specifies, verifying it belongs to the piece index we asked for
// and fits within buf. It returns the number of bytes written.
func ParsePiece(index int, buf []byte, m *Message) (int, error) {
	if m.ID != MsgPiece {
		return 0, fmt.Errorf("p2p: expected PIECE (id %d), got id %d", MsgPiece, m.ID)
	}
	if len(m.Payload) < 8 {
		return 0, fmt.Errorf("p2p: PIECE payload too short (%d bytes)", len(m.Payload))
	}
	parsedIndex := int(binary.BigEndian.Uint32(m.Payload[0:4]))
	if parsedIndex != index {
		return 0, fmt.Errorf("p2p: PIECE for index %d, expected %d", parsedIndex, index)
	}
	begin := int(binary.BigEndian.Uint32(m.Payload[4:8]))
	if begin >= len(buf) {
		return 0, fmt.Errorf("p2p: PIECE begin offset %d beyond buffer length %d", begin, len(buf))
	}
	block := m.Payload[8:]
	if begin+len(block) > len(buf) {
		return 0, fmt.Errorf("p2p: PIECE block of %d bytes at offset %d overflows buffer length %d", len(block), begin, len(buf))
	}
	copy(buf[begin:], block)
	return len(block), nil
}

// Bitfield records, one bit per piece, which pieces a peer claims to have.
// Bit 0 of byte 0 is piece index 0, per the wire format.
type Bitfield []byte

// HasPiece reports whether the bitfield marks index as present.
func (bf Bitfield) HasPiece(index int) bool {
	byteIndex := index / 8
	offset := index % 8
	if byteIndex < 0 || byteIndex >= len(bf) {
		return false
	}
	return bf[byteIndex]>>(7-offset)&1 != 0
}

// SetPiece marks index as present.
func (bf Bitfield) SetPiece(index int) {
	byteIndex := index / 8
	offset := index % 8
	if byteIndex < 0 || byteIndex >= len(bf) {
		return
	}
	bf[byteIndex] |= 1 << (7 - offset)
}
