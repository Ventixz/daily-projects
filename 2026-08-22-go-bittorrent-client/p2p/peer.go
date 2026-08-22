package p2p

import (
	"encoding/binary"
	"fmt"
	"net"
	"strconv"
)

const peerBytes = 6 // 4-byte IPv4 address + 2-byte port, big-endian

// Peer is one entry from a tracker's compact peer list.
type Peer struct {
	IP   net.IP
	Port uint16
}

func (p Peer) String() string {
	return net.JoinHostPort(p.IP.String(), strconv.Itoa(int(p.Port)))
}

// UnmarshalPeers decodes a tracker's compact peer list: a byte string that
// is a flat sequence of 6-byte peer entries.
func UnmarshalPeers(raw []byte) ([]Peer, error) {
	if len(raw)%peerBytes != 0 {
		return nil, fmt.Errorf("p2p: compact peer list length %d is not a multiple of %d", len(raw), peerBytes)
	}
	n := len(raw) / peerBytes
	peers := make([]Peer, n)
	for i := 0; i < n; i++ {
		off := i * peerBytes
		peers[i].IP = net.IP(raw[off : off+4])
		peers[i].Port = binary.BigEndian.Uint16(raw[off+4 : off+6])
	}
	return peers, nil
}
