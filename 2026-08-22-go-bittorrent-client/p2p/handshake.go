package p2p

import (
	"fmt"
	"io"
)

const protocolID = "BitTorrent protocol"

// Handshake is the fixed 68-byte message that opens every peer connection:
// <pstrlen:1><pstr:19><reserved:8><info_hash:20><peer_id:20>.
type Handshake struct {
	InfoHash [20]byte
	PeerID   [20]byte
}

// Serialize encodes the handshake to wire format.
func (h *Handshake) Serialize() []byte {
	buf := make([]byte, 0, 49+len(protocolID))
	buf = append(buf, byte(len(protocolID)))
	buf = append(buf, protocolID...)
	buf = append(buf, make([]byte, 8)...) // reserved, all zero: no extensions
	buf = append(buf, h.InfoHash[:]...)
	buf = append(buf, h.PeerID[:]...)
	return buf
}

// ReadHandshake reads and validates a handshake from r. It does not check
// that InfoHash matches an expected value -- callers doing that comparison
// get a clearer error message than this function could produce.
func ReadHandshake(r io.Reader) (*Handshake, error) {
	lenBuf := make([]byte, 1)
	if _, err := io.ReadFull(r, lenBuf); err != nil {
		return nil, fmt.Errorf("p2p: reading pstrlen: %w", err)
	}
	pstrlen := int(lenBuf[0])
	if pstrlen == 0 {
		return nil, fmt.Errorf("p2p: pstrlen is 0")
	}

	rest := make([]byte, pstrlen+8+20+20)
	if _, err := io.ReadFull(r, rest); err != nil {
		return nil, fmt.Errorf("p2p: reading handshake body: %w", err)
	}

	var infoHash, peerID [20]byte
	copy(infoHash[:], rest[pstrlen+8:pstrlen+8+20])
	copy(peerID[:], rest[pstrlen+8+20:])

	return &Handshake{InfoHash: infoHash, PeerID: peerID}, nil
}
