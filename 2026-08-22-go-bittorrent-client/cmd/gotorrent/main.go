// Command gotorrent downloads the single file described by a .torrent
// metainfo file. By default it announces to the torrent's tracker for a
// peer list; -peer bypasses that and connects to one peer directly, which
// is how this repo's demo works without a live tracker or public swarm.
package main

import (
	"crypto/rand"
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"

	"gotorrent/client"
	"gotorrent/p2p"
	"gotorrent/torrentfile"
	"gotorrent/tracker"
)

const listenPort = 6882

func main() {
	torrentPath := flag.String("torrent", "", "path to the .torrent file (required)")
	outPath := flag.String("out", "", "path to write the downloaded file to (required)")
	peerAddr := flag.String("peer", "", "host:port of a single peer to download from directly, bypassing the tracker")
	flag.Parse()

	if *torrentPath == "" || *outPath == "" {
		fmt.Fprintln(os.Stderr, "usage: gotorrent -torrent <file.torrent> -out <output> [-peer host:port]")
		os.Exit(2)
	}

	if err := run(*torrentPath, *outPath, *peerAddr); err != nil {
		fmt.Fprintln(os.Stderr, "gotorrent:", err)
		os.Exit(1)
	}
}

func run(torrentPath, outPath, peerAddr string) error {
	tf, err := torrentfile.Open(torrentPath)
	if err != nil {
		return fmt.Errorf("opening torrent: %w", err)
	}

	var peerID [20]byte
	if _, err := rand.Read(peerID[:]); err != nil {
		return fmt.Errorf("generating peer id: %w", err)
	}

	var peers []p2p.Peer
	if peerAddr != "" {
		p, err := parsePeerAddr(peerAddr)
		if err != nil {
			return err
		}
		peers = []p2p.Peer{p}
	} else {
		peers, err = tracker.RequestPeers(tf, peerID, listenPort)
		if err != nil {
			return fmt.Errorf("requesting peers: %w", err)
		}
		if len(peers) == 0 {
			return fmt.Errorf("tracker returned no peers")
		}
	}

	fmt.Fprintf(os.Stderr, "downloading %q (%d bytes, %d pieces) from %d peer(s)\n", tf.Name, tf.Length, len(tf.PieceHashes), len(peers))

	buf, err := client.Download(tf, peers, peerID)
	if err != nil {
		return fmt.Errorf("downloading: %w", err)
	}

	if err := os.WriteFile(outPath, buf, 0644); err != nil {
		return fmt.Errorf("writing output: %w", err)
	}
	fmt.Fprintf(os.Stderr, "wrote %d bytes to %s\n", len(buf), outPath)
	return nil
}

func parsePeerAddr(addr string) (p2p.Peer, error) {
	host, portStr, err := net.SplitHostPort(addr)
	if err != nil {
		return p2p.Peer{}, fmt.Errorf("invalid -peer %q: %w", addr, err)
	}
	ip := net.ParseIP(host)
	if ip == nil {
		resolved, err := net.ResolveIPAddr("ip4", host)
		if err != nil {
			return p2p.Peer{}, fmt.Errorf("resolving -peer host %q: %w", host, err)
		}
		ip = resolved.IP
	}
	port, err := strconv.Atoi(portStr)
	if err != nil || port <= 0 || port > 65535 {
		return p2p.Peer{}, fmt.Errorf("invalid -peer port in %q", addr)
	}
	return p2p.Peer{IP: ip, Port: uint16(port)}, nil
}
