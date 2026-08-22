package bencode

import (
	"bytes"
	"reflect"
	"strings"
	"testing"
)

func TestDecodeInt(t *testing.T) {
	cases := map[string]int64{
		"i4e":    4,
		"i0e":    0,
		"i-13e":  -13,
		"i9999e": 9999,
	}
	for in, want := range cases {
		got, err := Decode(strings.NewReader(in))
		if err != nil {
			t.Fatalf("Decode(%q): %v", in, err)
		}
		if got != want {
			t.Errorf("Decode(%q) = %v, want %v", in, got, want)
		}
	}
}

func TestDecodeString(t *testing.T) {
	got, err := Decode(strings.NewReader("4:spam"))
	if err != nil {
		t.Fatal(err)
	}
	if got != "spam" {
		t.Errorf("got %q, want %q", got, "spam")
	}

	got, err = Decode(strings.NewReader("0:"))
	if err != nil {
		t.Fatal(err)
	}
	if got != "" {
		t.Errorf("got %q, want empty string", got)
	}
}

func TestDecodeList(t *testing.T) {
	got, err := Decode(strings.NewReader("l4:spam4:eggsi7ee"))
	if err != nil {
		t.Fatal(err)
	}
	want := []interface{}{"spam", "eggs", int64(7)}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("got %#v, want %#v", got, want)
	}
}

func TestDecodeDict(t *testing.T) {
	got, err := Decode(strings.NewReader("d3:cow3:moo4:spam4:eggse"))
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]interface{}{"cow": "moo", "spam": "eggs"}
	if !reflect.DeepEqual(got, want) {
		t.Errorf("got %#v, want %#v", got, want)
	}
}

func TestDecodeNestedDict(t *testing.T) {
	// A dict value that is itself a dict -- the shape a .torrent file's
	// top-level dict actually has ("info" maps to another dict).
	in := "d4:infod6:lengthi100e4:name4:filee8:announce3:urle"
	got, err := Decode(strings.NewReader(in))
	if err != nil {
		t.Fatal(err)
	}
	dict := got.(map[string]interface{})
	if dict["announce"] != "url" {
		t.Errorf("announce = %v, want %q", dict["announce"], "url")
	}
	info := dict["info"].(map[string]interface{})
	if info["length"] != int64(100) || info["name"] != "file" {
		t.Errorf("info = %#v", info)
	}
}

func TestEncodeRoundTrip(t *testing.T) {
	cases := []string{
		"i4e",
		"i-13e",
		"4:spam",
		"0:",
		"l4:spam4:eggsi7ee",
		"d3:cow3:moo4:spam4:eggse",
	}
	for _, in := range cases {
		v, err := Decode(strings.NewReader(in))
		if err != nil {
			t.Fatalf("Decode(%q): %v", in, err)
		}
		var buf bytes.Buffer
		if err := Encode(&buf, v); err != nil {
			t.Fatalf("Encode(%v): %v", v, err)
		}
		if buf.String() != in {
			t.Errorf("round trip: Decode(%q) -> Encode -> %q", in, buf.String())
		}
	}
}

func TestEncodeSortsDictKeys(t *testing.T) {
	// Bencode requires dict keys in sorted byte order; a Go map has no
	// order at all, so Encode must impose it rather than iterate randomly.
	dict := map[string]interface{}{
		"zebra": int64(1),
		"apple": int64(2),
		"mango": int64(3),
	}
	var buf bytes.Buffer
	if err := Encode(&buf, dict); err != nil {
		t.Fatal(err)
	}
	want := "d5:applei2e5:mangoi3e5:zebrai1ee"
	if buf.String() != want {
		t.Errorf("got %q, want %q", buf.String(), want)
	}
}

func TestDecodeTruncatedInputErrors(t *testing.T) {
	cases := []string{"i4", "4:sp", "l4:spam", "d3:cow3:moo"}
	for _, in := range cases {
		if _, err := Decode(strings.NewReader(in)); err == nil {
			t.Errorf("Decode(%q): expected error, got nil", in)
		}
	}
}
