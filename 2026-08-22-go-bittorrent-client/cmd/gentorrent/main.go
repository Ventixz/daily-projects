// Command gentorrent builds a single-file .torrent metainfo file from a
// plain file on disk, for use with demoseeder/gotorrent's demo mode.
package main

import (
	"bytes"
	"crypto/sha1"
	"flag"
	"fmt"
	"os"

	"gotorrent/bencode"
)

func main() {
	inPath := flag.String("in", "", "path to the file to describe (required)")
	outPath := flag.String("out", "", "path to write the .torrent file to (required)")
	pieceLength := flag.Int("piece-length", 32768, "piece length in bytes")
	announce := flag.String("announce", "http://127.0.0.1:6969/announce", "announce URL to embed (unused in -peer demo mode)")
	flag.Parse()

	if *inPath == "" || *outPath == "" {
		fmt.Fprintln(os.Stderr, "usage: gentorrent -in <file> -out <file.torrent> [-piece-length 32768] [-announce url]")
		os.Exit(2)
	}

	content, err := os.ReadFile(*inPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "gentorrent:", err)
		os.Exit(1)
	}

	var pieces bytes.Buffer
	for begin := 0; begin < len(content); begin += *pieceLength {
		end := begin + *pieceLength
		if end > len(content) {
			end = len(content)
		}
		hash := sha1.Sum(content[begin:end])
		pieces.Write(hash[:])
	}

	info := map[string]interface{}{
		"piece length": int64(*pieceLength),
		"length":       int64(len(content)),
		"name":         baseName(*inPath),
		"pieces":       pieces.String(),
	}
	top := map[string]interface{}{
		"announce": *announce,
		"info":     info,
	}

	f, err := os.Create(*outPath)
	if err != nil {
		fmt.Fprintln(os.Stderr, "gentorrent:", err)
		os.Exit(1)
	}
	defer f.Close()

	if err := bencode.Encode(f, top); err != nil {
		fmt.Fprintln(os.Stderr, "gentorrent:", err)
		os.Exit(1)
	}

	fmt.Fprintf(os.Stderr, "gentorrent: wrote %s (%d bytes, %d pieces)\n", *outPath, len(content), pieces.Len()/20)
}

func baseName(path string) string {
	for i := len(path) - 1; i >= 0; i-- {
		if path[i] == '/' {
			return path[i+1:]
		}
	}
	return path
}
