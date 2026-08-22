// Package torrentfile parses .torrent metainfo files (single-file torrents
// only -- no multi-file "files" list) and derives the SHA-1 info_hash and
// per-piece hashes a client needs to talk to a tracker and verify downloads.
package torrentfile

import (
	"bytes"
	"crypto/sha1"
	"errors"
	"fmt"
	"io"
	"os"

	"gotorrent/bencode"
)

const hashLen = 20 // SHA-1 output length, and the size of one piece hash

// TorrentFile is the parsed, ready-to-use form of a .torrent file's metainfo.
type TorrentFile struct {
	Announce    string
	InfoHash    [hashLen]byte
	PieceHashes [][hashLen]byte
	PieceLength int
	Length      int
	Name        string
}

// Open reads and parses a .torrent file from path.
func Open(path string) (*TorrentFile, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return Parse(f)
}

// Parse reads and parses a .torrent file's bencoded metainfo from r.
func Parse(r io.Reader) (*TorrentFile, error) {
	val, err := bencode.Decode(r)
	if err != nil {
		return nil, fmt.Errorf("torrentfile: %w", err)
	}
	dict, ok := val.(map[string]interface{})
	if !ok {
		return nil, errors.New("torrentfile: top-level bencode value is not a dict")
	}

	announce, ok := dict["announce"].(string)
	if !ok {
		return nil, errors.New("torrentfile: missing or non-string \"announce\"")
	}

	infoRaw, ok := dict["info"]
	if !ok {
		return nil, errors.New("torrentfile: missing \"info\" dict")
	}
	info, ok := infoRaw.(map[string]interface{})
	if !ok {
		return nil, errors.New("torrentfile: \"info\" is not a dict")
	}

	if _, multiFile := info["files"]; multiFile {
		return nil, errors.New("torrentfile: multi-file torrents are not supported")
	}

	// The info_hash is SHA-1 over the *exact* bencoding of the info dict.
	// Re-encoding what we just decoded reproduces those bytes because
	// Encode always emits sorted keys, which is what a spec-conformant
	// .torrent file already used.
	var infoBuf bytes.Buffer
	if err := bencode.Encode(&infoBuf, info); err != nil {
		return nil, fmt.Errorf("torrentfile: re-encoding info dict: %w", err)
	}
	infoHash := sha1.Sum(infoBuf.Bytes())

	piecesStr, ok := info["pieces"].(string)
	if !ok {
		return nil, errors.New("torrentfile: missing or non-string \"pieces\"")
	}
	if len(piecesStr)%hashLen != 0 {
		return nil, fmt.Errorf("torrentfile: \"pieces\" length %d is not a multiple of %d", len(piecesStr), hashLen)
	}
	numPieces := len(piecesStr) / hashLen
	pieceHashes := make([][hashLen]byte, numPieces)
	for i := 0; i < numPieces; i++ {
		copy(pieceHashes[i][:], piecesStr[i*hashLen:(i+1)*hashLen])
	}

	pieceLength, ok := info["piece length"].(int64)
	if !ok {
		return nil, errors.New("torrentfile: missing or non-integer \"piece length\"")
	}
	length, ok := info["length"].(int64)
	if !ok {
		return nil, errors.New("torrentfile: missing or non-integer \"length\"")
	}
	name, ok := info["name"].(string)
	if !ok {
		return nil, errors.New("torrentfile: missing or non-string \"name\"")
	}

	return &TorrentFile{
		Announce:    announce,
		InfoHash:    infoHash,
		PieceHashes: pieceHashes,
		PieceLength: int(pieceLength),
		Length:      int(length),
		Name:        name,
	}, nil
}
