package main

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"testing"

	"url-shortener/internal/shortener"
)

func newTestServer(t *testing.T) http.Handler {
	t.Helper()
	store, err := shortener.NewStore(filepath.Join(t.TempDir(), "links.json"))
	if err != nil {
		t.Fatalf("NewStore: %v", err)
	}
	return newServer(store, "static")
}

func TestShortenAndRedirect(t *testing.T) {
	srv := newTestServer(t)

	body, _ := json.Marshal(shortenRequest{URL: "https://example.com/some/long/path"})
	req := httptest.NewRequest(http.MethodPost, "/api/shorten", bytes.NewReader(body))
	rec := httptest.NewRecorder()
	srv.ServeHTTP(rec, req)

	if rec.Code != http.StatusCreated {
		t.Fatalf("POST /api/shorten status = %d, body = %s", rec.Code, rec.Body)
	}
	var resp shortenResponse
	if err := json.Unmarshal(rec.Body.Bytes(), &resp); err != nil {
		t.Fatalf("decoding response: %v", err)
	}
	if resp.Code == "" {
		t.Fatal("response code is empty")
	}

	redirectReq := httptest.NewRequest(http.MethodGet, "/"+resp.Code, nil)
	redirectRec := httptest.NewRecorder()
	srv.ServeHTTP(redirectRec, redirectReq)

	if redirectRec.Code != http.StatusFound {
		t.Fatalf("GET /%s status = %d, want %d", resp.Code, redirectRec.Code, http.StatusFound)
	}
	if loc := redirectRec.Header().Get("Location"); loc != "https://example.com/some/long/path" {
		t.Errorf("Location = %q, want the original URL", loc)
	}

	statsReq := httptest.NewRequest(http.MethodGet, "/api/stats/"+resp.Code, nil)
	statsRec := httptest.NewRecorder()
	srv.ServeHTTP(statsRec, statsReq)

	var link shortener.Link
	if err := json.Unmarshal(statsRec.Body.Bytes(), &link); err != nil {
		t.Fatalf("decoding stats: %v", err)
	}
	if link.Clicks != 1 {
		t.Errorf("Clicks after one redirect = %d, want 1", link.Clicks)
	}
}

func TestShortenRejectsInvalidURL(t *testing.T) {
	srv := newTestServer(t)

	body, _ := json.Marshal(shortenRequest{URL: "not-a-url"})
	req := httptest.NewRequest(http.MethodPost, "/api/shorten", bytes.NewReader(body))
	rec := httptest.NewRecorder()
	srv.ServeHTTP(rec, req)

	if rec.Code != http.StatusBadRequest {
		t.Errorf("status = %d, want %d", rec.Code, http.StatusBadRequest)
	}
}

func TestRedirectUnknownCode(t *testing.T) {
	srv := newTestServer(t)

	req := httptest.NewRequest(http.MethodGet, "/does-not-exist", nil)
	rec := httptest.NewRecorder()
	srv.ServeHTTP(rec, req)

	if rec.Code != http.StatusNotFound {
		t.Errorf("status = %d, want %d", rec.Code, http.StatusNotFound)
	}
}
