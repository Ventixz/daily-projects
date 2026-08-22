// Package bencode implements the encoding used by .torrent files and
// tracker responses: integers as "i<n>e", byte strings as "<len>:<bytes>",
// lists as "l...e", and dictionaries as "d...e" with lexically sorted keys.
package bencode

import (
	"bufio"
	"errors"
	"fmt"
	"io"
	"sort"
	"strconv"
)

// Decode reads a single bencoded value from r and returns it as one of:
// int64, string, []interface{}, or map[string]interface{}.
func Decode(r io.Reader) (interface{}, error) {
	br, ok := r.(*bufio.Reader)
	if !ok {
		br = bufio.NewReader(r)
	}
	return decodeValue(br)
}

func decodeValue(r *bufio.Reader) (interface{}, error) {
	b, err := r.Peek(1)
	if err != nil {
		return nil, err
	}
	switch {
	case b[0] == 'i':
		return decodeInt(r)
	case b[0] == 'l':
		return decodeList(r)
	case b[0] == 'd':
		return decodeDict(r)
	case b[0] >= '0' && b[0] <= '9':
		return decodeString(r)
	default:
		return nil, fmt.Errorf("bencode: unexpected leading byte %q", b[0])
	}
}

func decodeInt(r *bufio.Reader) (int64, error) {
	if _, err := r.ReadByte(); err != nil { // consume 'i'
		return 0, err
	}
	tok, err := r.ReadString('e')
	if err != nil {
		return 0, err
	}
	tok = tok[:len(tok)-1] // drop trailing 'e'
	if tok == "" {
		return 0, errors.New("bencode: empty integer")
	}
	n, err := strconv.ParseInt(tok, 10, 64)
	if err != nil {
		return 0, fmt.Errorf("bencode: invalid integer %q: %w", tok, err)
	}
	return n, nil
}

func decodeString(r *bufio.Reader) (string, error) {
	lenTok, err := r.ReadString(':')
	if err != nil {
		return "", err
	}
	lenTok = lenTok[:len(lenTok)-1]
	n, err := strconv.Atoi(lenTok)
	if err != nil {
		return "", fmt.Errorf("bencode: invalid string length %q: %w", lenTok, err)
	}
	if n < 0 {
		return "", fmt.Errorf("bencode: negative string length %d", n)
	}
	buf := make([]byte, n)
	if _, err := io.ReadFull(r, buf); err != nil {
		return "", err
	}
	return string(buf), nil
}

func decodeList(r *bufio.Reader) ([]interface{}, error) {
	if _, err := r.ReadByte(); err != nil { // consume 'l'
		return nil, err
	}
	list := []interface{}{}
	for {
		b, err := r.Peek(1)
		if err != nil {
			return nil, err
		}
		if b[0] == 'e' {
			r.ReadByte()
			return list, nil
		}
		v, err := decodeValue(r)
		if err != nil {
			return nil, err
		}
		list = append(list, v)
	}
}

func decodeDict(r *bufio.Reader) (map[string]interface{}, error) {
	if _, err := r.ReadByte(); err != nil { // consume 'd'
		return nil, err
	}
	dict := map[string]interface{}{}
	for {
		b, err := r.Peek(1)
		if err != nil {
			return nil, err
		}
		if b[0] == 'e' {
			r.ReadByte()
			return dict, nil
		}
		key, err := decodeString(r)
		if err != nil {
			return nil, fmt.Errorf("bencode: dict key: %w", err)
		}
		val, err := decodeValue(r)
		if err != nil {
			return nil, fmt.Errorf("bencode: dict value for key %q: %w", key, err)
		}
		dict[key] = val
	}
}

// Encode writes v in canonical bencoded form. Dictionary keys are sorted
// lexically by byte value, as the spec requires -- this is what lets a
// decoded "info" dict be re-encoded and hashed back into the same
// info_hash the original .torrent file was built with.
func Encode(w io.Writer, v interface{}) error {
	switch val := v.(type) {
	case int:
		return encodeInt(w, int64(val))
	case int64:
		return encodeInt(w, val)
	case string:
		return encodeString(w, val)
	case []byte:
		return encodeString(w, string(val))
	case []interface{}:
		return encodeList(w, val)
	case map[string]interface{}:
		return encodeDict(w, val)
	default:
		return fmt.Errorf("bencode: unsupported type %T", v)
	}
}

func encodeInt(w io.Writer, n int64) error {
	_, err := fmt.Fprintf(w, "i%de", n)
	return err
}

func encodeString(w io.Writer, s string) error {
	_, err := fmt.Fprintf(w, "%d:%s", len(s), s)
	return err
}

func encodeList(w io.Writer, list []interface{}) error {
	if _, err := io.WriteString(w, "l"); err != nil {
		return err
	}
	for _, v := range list {
		if err := Encode(w, v); err != nil {
			return err
		}
	}
	_, err := io.WriteString(w, "e")
	return err
}

func encodeDict(w io.Writer, dict map[string]interface{}) error {
	keys := make([]string, 0, len(dict))
	for k := range dict {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	if _, err := io.WriteString(w, "d"); err != nil {
		return err
	}
	for _, k := range keys {
		if err := encodeString(w, k); err != nil {
			return err
		}
		if err := Encode(w, dict[k]); err != nil {
			return err
		}
	}
	_, err := io.WriteString(w, "e")
	return err
}
