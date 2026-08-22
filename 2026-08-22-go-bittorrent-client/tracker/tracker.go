// Package tracker requests a peer list from a torrent's HTTP announce URL.
package tracker

import (
	"bytes"
	"fmt"
	"net/http"
	"net/url"
	"strconv"
	"time"

	"gotorrent/bencode"
	"gotorrent/p2p"
	"gotorrent/torrentfile"
)

const requestTimeout = 15 * time.Second

// BuildAnnounceURL builds the GET URL for a tracker announce request, per
// the standard HTTP tracker protocol (BEP 3).
func BuildAnnounceURL(tf *torrentfile.TorrentFile, peerID [20]byte, port uint16) (string, error) {
	base, err := url.Parse(tf.Announce)
	if err != nil {
		return "", fmt.Errorf("tracker: parsing announce URL: %w", err)
	}
	params := url.Values{
		"info_hash":  []string{string(tf.InfoHash[:])},
		"peer_id":    []string{string(peerID[:])},
		"port":       []string{strconv.Itoa(int(port))},
		"uploaded":   []string{"0"},
		"downloaded": []string{"0"},
		"left":       []string{strconv.Itoa(tf.Length)},
		"compact":    []string{"1"},
	}
	base.RawQuery = params.Encode()
	return base.String(), nil
}

// RequestPeers announces to tf's tracker and returns the peer list it hands
// back. This makes a real outbound HTTP request; tests exercise it against
// an httptest server rather than a live tracker.
func RequestPeers(tf *torrentfile.TorrentFile, peerID [20]byte, port uint16) ([]p2p.Peer, error) {
	announceURL, err := BuildAnnounceURL(tf, peerID, port)
	if err != nil {
		return nil, err
	}

	client := &http.Client{Timeout: requestTimeout}
	resp, err := client.Get(announceURL)
	if err != nil {
		return nil, fmt.Errorf("tracker: request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("tracker: HTTP status %s", resp.Status)
	}

	val, err := bencode.Decode(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("tracker: decoding response: %w", err)
	}
	dict, ok := val.(map[string]interface{})
	if !ok {
		return nil, fmt.Errorf("tracker: response is not a bencoded dict")
	}
	if reason, ok := dict["failure reason"].(string); ok {
		return nil, fmt.Errorf("tracker: %s", reason)
	}

	peersStr, ok := dict["peers"].(string)
	if !ok {
		return nil, fmt.Errorf("tracker: response missing compact \"peers\" string")
	}

	return p2p.UnmarshalPeers([]byte(peersStr))
}

// EncodeResponse bencodes a tracker response dict -- used by tests (and the
// demo seeder) to produce a well-formed response without a real tracker.
func EncodeResponse(interval int, peers []byte) ([]byte, error) {
	dict := map[string]interface{}{
		"interval": int64(interval),
		"peers":    string(peers),
	}
	var buf bytes.Buffer
	if err := bencode.Encode(&buf, dict); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}
