package client

import (
	"crypto/sha1"
	"fmt"
	"log"
	"time"

	"gotorrent/p2p"
	"gotorrent/torrentfile"
)

const (
	maxBlockSize = 16384 // 16 KiB: the block size real BitTorrent peers expect
	maxBacklog   = 5     // in-flight block requests per peer
	pieceTimeout = 30 * time.Second
)

type pieceWork struct {
	index  int
	hash   [20]byte
	length int
}

type pieceResult struct {
	index int
	buf   []byte
}

// Download fetches every piece of tf from peers and returns the assembled,
// hash-verified file contents. It fans work for all pieces out across one
// goroutine per peer; each goroutine pulls the next unclaimed piece from a
// shared queue, and a piece that fails hash verification (or whose peer
// errors out) goes back on the queue for another peer to try.
func Download(tf *torrentfile.TorrentFile, peers []p2p.Peer, peerID [20]byte) ([]byte, error) {
	if len(peers) == 0 {
		return nil, fmt.Errorf("client: no peers to download from")
	}

	work := make(chan pieceWork, len(tf.PieceHashes))
	results := make(chan pieceResult, len(tf.PieceHashes))
	for i, hash := range tf.PieceHashes {
		work <- pieceWork{index: i, hash: hash, length: pieceLength(tf, i)}
	}

	for _, peer := range peers {
		go downloadWorker(peer, peerID, tf.InfoHash, work, results)
	}

	buf := make([]byte, tf.Length)
	received := 0
	for received < len(tf.PieceHashes) {
		res := <-results
		begin, end := pieceBounds(tf, res.index)
		copy(buf[begin:end], res.buf)
		received++
	}
	close(work)

	return buf, nil
}

func pieceLength(tf *torrentfile.TorrentFile, index int) int {
	begin, end := pieceBounds(tf, index)
	return end - begin
}

func pieceBounds(tf *torrentfile.TorrentFile, index int) (begin, end int) {
	begin = index * tf.PieceLength
	end = begin + tf.PieceLength
	if end > tf.Length {
		end = tf.Length
	}
	return begin, end
}

func downloadWorker(peer p2p.Peer, peerID, infoHash [20]byte, work chan pieceWork, results chan<- pieceResult) {
	c, err := New(peer, peerID, infoHash)
	if err != nil {
		log.Printf("client: skipping peer %s: %v", peer, err)
		return
	}
	defer c.Conn.Close()

	if err := c.SendInterested(); err != nil {
		log.Printf("client: peer %s: sending interested: %v", peer, err)
		return
	}

	for pw := range work {
		if c.Bitfield != nil && len(c.Bitfield) > 0 && !c.Bitfield.HasPiece(pw.index) {
			work <- pw // this peer doesn't have it; let another try
			continue
		}

		buf, err := downloadPiece(c, pw)
		if err != nil {
			log.Printf("client: peer %s: piece %d: %v", peer, pw.index, err)
			work <- pw
			return
		}

		got := sha1.Sum(buf)
		if got != pw.hash {
			log.Printf("client: peer %s: piece %d failed hash check", peer, pw.index)
			work <- pw
			continue
		}

		results <- pieceResult{index: pw.index, buf: buf}
	}
}

func downloadPiece(c *Client, pw pieceWork) ([]byte, error) {
	buf := make([]byte, pw.length)
	c.Conn.SetDeadline(time.Now().Add(pieceTimeout))
	defer c.Conn.SetDeadline(time.Time{})

	downloaded, requested, backlog := 0, 0, 0
	for downloaded < pw.length {
		if !c.Choked {
			for backlog < maxBacklog && requested < pw.length {
				blockSize := maxBlockSize
				if pw.length-requested < blockSize {
					blockSize = pw.length - requested
				}
				if err := c.SendRequest(pw.index, requested, blockSize); err != nil {
					return nil, fmt.Errorf("sending request: %w", err)
				}
				backlog++
				requested += blockSize
			}
		}

		msg, err := c.Read()
		if err != nil {
			return nil, fmt.Errorf("reading message: %w", err)
		}
		if msg == nil {
			continue // keep-alive
		}
		if msg.ID != p2p.MsgPiece {
			continue // choke/unchoke/have already applied by c.Read
		}

		n, err := p2p.ParsePiece(pw.index, buf, msg)
		if err != nil {
			return nil, fmt.Errorf("parsing piece: %w", err)
		}
		downloaded += n
		backlog--
	}

	return buf, nil
}
