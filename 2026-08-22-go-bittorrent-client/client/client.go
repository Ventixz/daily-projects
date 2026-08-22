// Package client drives a single peer connection through the wire protocol
// handshake and message exchange, and orchestrates downloading a whole
// torrent's pieces across a pool of peers.
package client

import (
	"fmt"
	"net"
	"time"

	"gotorrent/p2p"
)

const handshakeTimeout = 5 * time.Second

// Client is one open, handshaken connection to a peer.
type Client struct {
	Conn     net.Conn
	Choked   bool
	Bitfield p2p.Bitfield
	peer     p2p.Peer
	peerID   [20]byte
	infoHash [20]byte
}

// New dials peer, performs the handshake, and reads the bitfield message
// peers conventionally send immediately afterward.
func New(peer p2p.Peer, peerID, infoHash [20]byte) (*Client, error) {
	conn, err := net.DialTimeout("tcp", peer.String(), handshakeTimeout)
	if err != nil {
		return nil, fmt.Errorf("client: dial %s: %w", peer, err)
	}

	conn.SetDeadline(time.Now().Add(handshakeTimeout))
	if err := doHandshake(conn, peerID, infoHash); err != nil {
		conn.Close()
		return nil, err
	}

	bf, err := readBitfield(conn)
	if err != nil {
		conn.Close()
		return nil, err
	}
	conn.SetDeadline(time.Time{})

	return &Client{
		Conn:     conn,
		Choked:   true,
		Bitfield: bf,
		peer:     peer,
		peerID:   peerID,
		infoHash: infoHash,
	}, nil
}

func doHandshake(conn net.Conn, peerID, infoHash [20]byte) error {
	req := &p2p.Handshake{InfoHash: infoHash, PeerID: peerID}
	if _, err := conn.Write(req.Serialize()); err != nil {
		return fmt.Errorf("client: sending handshake: %w", err)
	}
	resp, err := p2p.ReadHandshake(conn)
	if err != nil {
		return fmt.Errorf("client: reading handshake: %w", err)
	}
	if resp.InfoHash != infoHash {
		return fmt.Errorf("client: peer's info_hash %x does not match expected %x", resp.InfoHash, infoHash)
	}
	return nil
}

// readBitfield reads the peer's opening message, which is conventionally
// (but not guaranteed to be) a bitfield. A peer with no pieces at all may
// send nothing here and go straight to other messages, so an empty
// bitfield is not itself an error.
func readBitfield(conn net.Conn) (p2p.Bitfield, error) {
	msg, err := p2p.Read(conn)
	if err != nil {
		return nil, fmt.Errorf("client: reading bitfield: %w", err)
	}
	if msg == nil {
		return p2p.Bitfield{}, nil // keep-alive instead of a bitfield
	}
	if msg.ID != p2p.MsgBitfield {
		return p2p.Bitfield{}, nil // peer skipped straight to another message
	}
	return p2p.Bitfield(msg.Payload), nil
}

// SendInterested tells the peer we want to download from it.
func (c *Client) SendInterested() error {
	_, err := c.Conn.Write((&p2p.Message{ID: p2p.MsgInterested}).Serialize())
	return err
}

// SendRequest asks the peer for one block.
func (c *Client) SendRequest(index, begin, length int) error {
	_, err := c.Conn.Write(p2p.FormatRequest(index, begin, length).Serialize())
	return err
}

// Read reads the next message (nil for keep-alive) and applies its effect
// on connection state (choke status, newly-announced pieces) before
// returning it to the caller.
func (c *Client) Read() (*p2p.Message, error) {
	msg, err := p2p.Read(c.Conn)
	if err != nil || msg == nil {
		return msg, err
	}
	switch msg.ID {
	case p2p.MsgChoke:
		c.Choked = true
	case p2p.MsgUnchoke:
		c.Choked = false
	case p2p.MsgHave:
		if index, err := p2p.ParseHave(msg); err == nil {
			if c.Bitfield == nil {
				c.Bitfield = p2p.Bitfield{}
			}
			needed := index/8 + 1
			for len(c.Bitfield) < needed {
				c.Bitfield = append(c.Bitfield, 0)
			}
			c.Bitfield.SetPiece(index)
		}
	}
	return msg, nil
}
