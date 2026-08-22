package client

import (
	"bytes"
	"crypto/sha1"
	"net"
	"testing"
	"time"

	"gotorrent/p2p"
	"gotorrent/torrentfile"
)

// runFakeSeeder accepts one connection on ln and serves content over the
// peer wire protocol: handshake, an all-ones bitfield, unchoke on request,
// and a PIECE reply for every REQUEST. It stands in for a real BitTorrent
// peer so the download path can be tested without live network access.
func runFakeSeeder(t *testing.T, ln net.Listener, content []byte, pieceLength, numPieces int, infoHash [20]byte) {
	t.Helper()
	conn, err := ln.Accept()
	if err != nil {
		return
	}
	defer conn.Close()

	if _, err := p2p.ReadHandshake(conn); err != nil {
		t.Errorf("seeder: reading handshake: %v", err)
		return
	}
	var seederID [20]byte
	copy(seederID[:], "-SEEDER-0000000000000")
	if _, err := conn.Write((&p2p.Handshake{InfoHash: infoHash, PeerID: seederID}).Serialize()); err != nil {
		t.Errorf("seeder: writing handshake: %v", err)
		return
	}

	bf := make(p2p.Bitfield, (numPieces+7)/8)
	for i := 0; i < numPieces; i++ {
		bf.SetPiece(i)
	}
	if _, err := conn.Write((&p2p.Message{ID: p2p.MsgBitfield, Payload: bf}).Serialize()); err != nil {
		t.Errorf("seeder: writing bitfield: %v", err)
		return
	}

	for {
		msg, err := p2p.Read(conn)
		if err != nil {
			return // client closed the connection when it was done
		}
		if msg == nil {
			continue
		}
		switch msg.ID {
		case p2p.MsgInterested:
			conn.Write((&p2p.Message{ID: p2p.MsgUnchoke}).Serialize())
		case p2p.MsgRequest:
			index, begin, length, err := p2p.ParseRequest(msg)
			if err != nil {
				t.Errorf("seeder: %v", err)
				return
			}
			block := pieceSlice(content, pieceLength, index)[begin : begin+length]
			conn.Write(p2p.FormatPiece(index, begin, block).Serialize())
		}
	}
}

// pieceSlice returns content's byte range for piece index, using the same
// "last piece may be short" convention as client/download.go.
func pieceSlice(content []byte, pieceLength, index int) []byte {
	begin := index * pieceLength
	end := begin + pieceLength
	if end > len(content) {
		end = len(content)
	}
	return content[begin:end]
}

func TestDownloadEndToEndOverLoopback(t *testing.T) {
	const contentLen = 100_000
	const pieceLength = 32768 // forces multiple 16 KiB blocks per piece

	content := make([]byte, contentLen)
	for i := range content {
		content[i] = byte((i*37 + 11) % 251) // deterministic, non-repeating filler
	}

	numPieces := (contentLen + pieceLength - 1) / pieceLength
	pieceHashes := make([][20]byte, numPieces)
	for i := 0; i < numPieces; i++ {
		begin := i * pieceLength
		end := begin + pieceLength
		if end > contentLen {
			end = contentLen
		}
		pieceHashes[i] = sha1.Sum(content[begin:end])
	}

	var infoHash [20]byte
	copy(infoHash[:], "test-info-hash-20by!")

	tf := &torrentfile.TorrentFile{
		Announce:    "http://unused.example/announce",
		InfoHash:    infoHash,
		PieceHashes: pieceHashes,
		PieceLength: pieceLength,
		Length:      contentLen,
		Name:        "loopback-demo",
	}

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	defer ln.Close()

	go runFakeSeeder(t, ln, content, pieceLength, numPieces, infoHash)

	addr := ln.Addr().(*net.TCPAddr)
	peers := []p2p.Peer{{IP: addr.IP, Port: uint16(addr.Port)}}
	var peerID [20]byte
	copy(peerID[:], "-GT0001-clientpeerid")

	type result struct {
		buf []byte
		err error
	}
	done := make(chan result, 1)
	go func() {
		buf, err := Download(tf, peers, peerID)
		done <- result{buf, err}
	}()

	select {
	case r := <-done:
		if r.err != nil {
			t.Fatalf("Download: %v", r.err)
		}
		if !bytes.Equal(r.buf, content) {
			t.Fatalf("downloaded %d bytes did not match the original %d bytes", len(r.buf), len(content))
		}
	case <-time.After(10 * time.Second):
		t.Fatal("Download did not complete within 10s")
	}
}
