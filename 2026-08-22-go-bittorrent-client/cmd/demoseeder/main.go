// Command demoseeder serves a single file over the BitTorrent peer wire
// protocol to exactly one connecting peer, so gotorrent can be demonstrated
// end-to-end without a live tracker or public swarm.
package main

import (
	"crypto/rand"
	"flag"
	"fmt"
	"net"
	"os"

	"gotorrent/p2p"
)

func main() {
	filePath := flag.String("file", "", "path to the file to seed (required)")
	port := flag.Int("port", 6881, "TCP port to listen on")
	pieceLength := flag.Int("piece-length", 32768, "piece length in bytes; must match the .torrent's")
	flag.Parse()

	if *filePath == "" {
		fmt.Fprintln(os.Stderr, "usage: demoseeder -file <path> [-port 6881] [-piece-length 32768]")
		os.Exit(2)
	}

	content, err := os.ReadFile(*filePath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "demoseeder:", err)
		os.Exit(1)
	}

	numPieces := (len(content) + *pieceLength - 1) / *pieceLength

	ln, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", *port))
	if err != nil {
		fmt.Fprintln(os.Stderr, "demoseeder:", err)
		os.Exit(1)
	}
	fmt.Fprintf(os.Stderr, "demoseeder: serving %s (%d bytes, %d pieces) on %s\n", *filePath, len(content), numPieces, ln.Addr())

	conn, err := ln.Accept()
	if err != nil {
		fmt.Fprintln(os.Stderr, "demoseeder:", err)
		os.Exit(1)
	}
	defer conn.Close()

	if err := serve(conn, content, *pieceLength, numPieces); err != nil {
		fmt.Fprintln(os.Stderr, "demoseeder:", err)
		os.Exit(1)
	}
}

func serve(conn net.Conn, content []byte, pieceLength, numPieces int) error {
	req, err := p2p.ReadHandshake(conn)
	if err != nil {
		return fmt.Errorf("reading handshake: %w", err)
	}

	var seederID [20]byte
	rand.Read(seederID[:])
	if _, err := conn.Write((&p2p.Handshake{InfoHash: req.InfoHash, PeerID: seederID}).Serialize()); err != nil {
		return fmt.Errorf("writing handshake: %w", err)
	}

	bf := make(p2p.Bitfield, (numPieces+7)/8)
	for i := 0; i < numPieces; i++ {
		bf.SetPiece(i)
	}
	if _, err := conn.Write((&p2p.Message{ID: p2p.MsgBitfield, Payload: bf}).Serialize()); err != nil {
		return fmt.Errorf("writing bitfield: %w", err)
	}

	for {
		msg, err := p2p.Read(conn)
		if err != nil {
			return nil // peer disconnected; nothing left to serve
		}
		if msg == nil {
			continue
		}
		switch msg.ID {
		case p2p.MsgInterested:
			if _, err := conn.Write((&p2p.Message{ID: p2p.MsgUnchoke}).Serialize()); err != nil {
				return fmt.Errorf("writing unchoke: %w", err)
			}
		case p2p.MsgRequest:
			index, begin, length, err := p2p.ParseRequest(msg)
			if err != nil {
				return err
			}
			pieceStart := index * pieceLength
			pieceEnd := pieceStart + pieceLength
			if pieceEnd > len(content) {
				pieceEnd = len(content)
			}
			block := content[pieceStart:pieceEnd][begin : begin+length]
			if _, err := conn.Write(p2p.FormatPiece(index, begin, block).Serialize()); err != nil {
				return fmt.Errorf("writing piece: %w", err)
			}
		}
	}
}
