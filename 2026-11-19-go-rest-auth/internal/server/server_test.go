package server

import (
	"bytes"
	"crypto/tls"
	"crypto/x509"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"restauth/internal/auth"
	"restauth/internal/password"
	"restauth/internal/taskstore"
	"restauth/internal/tlsutil"
)

// startServer runs the real handler behind our own TLS policy and a freshly
// generated certificate, and returns a client that trusts only that cert.
func startServer(t *testing.T) (*httptest.Server, *http.Client, []byte) {
	t.Helper()
	var lines []string
	for _, u := range []string{"alice", "bob"} {
		h, err := password.Hash(u+"-secret", 1000)
		if err != nil {
			t.Fatal(err)
		}
		lines = append(lines, u+":"+h)
	}
	users, err := auth.LoadUsers(strings.NewReader(strings.Join(lines, "\n")))
	if err != nil {
		t.Fatal(err)
	}
	certPEM, keyPEM, err := tlsutil.SelfSigned([]string{"127.0.0.1"}, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	cert, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		t.Fatal(err)
	}
	ts := httptest.NewUnstartedServer(New(taskstore.New(), users, "tasks"))
	ts.TLS = tlsutil.ServerConfig(cert)
	ts.StartTLS()
	t.Cleanup(ts.Close)

	pool := x509.NewCertPool()
	pool.AppendCertsFromPEM(certPEM)
	client := &http.Client{Transport: &http.Transport{TLSClientConfig: &tls.Config{RootCAs: pool}}}
	return ts, client, certPEM
}

type call struct {
	method, path, user, body string
	contentType              string
}

func do(t *testing.T, ts *httptest.Server, c *http.Client, k call) (*http.Response, string) {
	t.Helper()
	req, err := http.NewRequest(k.method, ts.URL+k.path, strings.NewReader(k.body))
	if err != nil {
		t.Fatal(err)
	}
	if k.user != "" {
		req.SetBasicAuth(k.user, k.user+"-secret")
	}
	if k.body != "" {
		ct := k.contentType
		if ct == "" {
			ct = "application/json"
		}
		req.Header.Set("Content-Type", ct)
	}
	resp, err := c.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	b, _ := io.ReadAll(resp.Body)
	return resp, string(b)
}

func TestFullFlowWithOwnership(t *testing.T) {
	ts, c, _ := startServer(t)

	resp, body := do(t, ts, c, call{method: "POST", path: "/task/", user: "alice",
		body: `{"text":"write tests","tags":["go","auth"],"due":"2026-11-20T10:00:00Z"}`})
	if resp.StatusCode != 201 {
		t.Fatalf("create: %d %s", resp.StatusCode, body)
	}
	var created struct{ ID int }
	json.Unmarshal([]byte(body), &created)

	resp, body = do(t, ts, c, call{method: "GET", path: "/task/0/", user: "alice"})
	if resp.StatusCode != 200 || !strings.Contains(body, `"owner":"alice"`) {
		t.Fatalf("get own task: %d %s", resp.StatusCode, body)
	}
	if cc := resp.Header.Get("Cache-Control"); cc != "no-store" {
		t.Errorf("Cache-Control = %q", cc)
	}

	// Bob is authenticated but not authorized: same 404 as a missing task.
	respBob, bodyBob := do(t, ts, c, call{method: "GET", path: "/task/0/", user: "bob"})
	respMissing, bodyMissing := do(t, ts, c, call{method: "GET", path: "/task/999/", user: "bob"})
	if respBob.StatusCode != 404 || respMissing.StatusCode != 404 || bodyBob != bodyMissing {
		t.Fatalf("other user's task should look missing: %d %q vs %d %q",
			respBob.StatusCode, bodyBob, respMissing.StatusCode, bodyMissing)
	}
	if resp, _ := do(t, ts, c, call{method: "DELETE", path: "/task/0/", user: "bob"}); resp.StatusCode != 404 {
		t.Fatalf("bob deleting alice's task: %d", resp.StatusCode)
	}
	if _, body := do(t, ts, c, call{method: "GET", path: "/task/", user: "bob"}); strings.TrimSpace(body) != "[]" {
		t.Fatalf("bob's list: %s", body)
	}

	if _, body := do(t, ts, c, call{method: "GET", path: "/tag/auth/", user: "alice"}); !strings.Contains(body, "write tests") {
		t.Fatalf("by tag: %s", body)
	}
	if _, body := do(t, ts, c, call{method: "GET", path: "/due/2026/11/20/", user: "alice"}); !strings.Contains(body, "write tests") {
		t.Fatalf("by due: %s", body)
	}
	if resp, _ := do(t, ts, c, call{method: "GET", path: "/due/2026/13/20/", user: "alice"}); resp.StatusCode != 400 {
		t.Fatalf("bad month: %d", resp.StatusCode)
	}
	if _, body := do(t, ts, c, call{method: "GET", path: "/whoami", user: "bob"}); !strings.Contains(body, `"bob"`) {
		t.Fatalf("whoami: %s", body)
	}
	if resp, _ := do(t, ts, c, call{method: "DELETE", path: "/task/0/", user: "alice"}); resp.StatusCode != 204 {
		t.Fatalf("delete own: %d", resp.StatusCode)
	}
}

func TestAuthRequiredExceptHealthz(t *testing.T) {
	ts, c, _ := startServer(t)
	resp, _ := do(t, ts, c, call{method: "GET", path: "/task/"})
	if resp.StatusCode != 401 || !strings.HasPrefix(resp.Header.Get("WWW-Authenticate"), "Basic ") {
		t.Fatalf("no creds: %d %q", resp.StatusCode, resp.Header.Get("WWW-Authenticate"))
	}
	// Unknown routes also need auth, so they don't reveal what exists.
	if resp, _ := do(t, ts, c, call{method: "GET", path: "/admin/"}); resp.StatusCode != 401 {
		t.Fatalf("unknown route without creds: %d", resp.StatusCode)
	}
	resp, body := do(t, ts, c, call{method: "GET", path: "/healthz"})
	if resp.StatusCode != 200 || body != "ok\n" {
		t.Fatalf("healthz: %d %q", resp.StatusCode, body)
	}
	if hsts := resp.Header.Get("Strict-Transport-Security"); !strings.HasPrefix(hsts, "max-age=") {
		t.Fatalf("HSTS header = %q", hsts)
	}
}

func TestInputValidation(t *testing.T) {
	ts, c, _ := startServer(t)
	cases := []struct {
		name string
		k    call
		want int
	}{
		{"broken json", call{body: `{"text":`}, 400},
		{"unknown field", call{body: `{"text":"x","owner":"bob"}`}, 400},
		{"missing text", call{body: `{"tags":["a"]}`}, 400},
		{"wrong content type", call{body: `{"text":"x"}`, contentType: "text/plain"}, 415},
		{"too large", call{body: `{"text":"` + strings.Repeat("a", 70000) + `"}`}, 413},
		{"bad id", call{method: "GET", path: "/task/abc/"}, 400},
	}
	for _, tc := range cases {
		k := tc.k
		k.user = "alice"
		if k.method == "" {
			k.method, k.path = "POST", "/task/"
		}
		if resp, body := do(t, ts, c, k); resp.StatusCode != tc.want {
			t.Errorf("%s: status %d, want %d (%s)", tc.name, resp.StatusCode, tc.want, body)
		}
	}
}

// The client can't be tricked into trusting the server without the cert, and
// clients that can't speak TLS 1.3 are refused at the handshake.
func TestTLSPolicy(t *testing.T) {
	ts, _, certPEM := startServer(t)

	if _, err := http.Get(ts.URL + "/healthz"); err == nil {
		t.Fatal("default client trusted a self-signed cert")
	}

	pool := x509.NewCertPool()
	pool.AppendCertsFromPEM(certPEM)
	old := &http.Client{Transport: &http.Transport{TLSClientConfig: &tls.Config{
		RootCAs: pool, MaxVersion: tls.VersionTLS12,
	}}}
	if _, err := old.Get(ts.URL + "/healthz"); err == nil {
		t.Fatal("TLS 1.2 client was accepted")
	}

	conn, err := tls.Dial("tcp", ts.Listener.Addr().String(), &tls.Config{RootCAs: pool})
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	if v := conn.ConnectionState().Version; v != tls.VersionTLS13 {
		t.Fatalf("negotiated %x, want TLS 1.3", v)
	}
}

func TestPlainHTTPIsRefused(t *testing.T) {
	// The same handler mounted on a plain listener must not process credentials.
	ts := httptest.NewServer(New(taskstore.New(), denyAll{}, "tasks"))
	defer ts.Close()
	req, _ := http.NewRequest("GET", ts.URL+"/task/", nil)
	req.SetBasicAuth("alice", "alice-secret")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	resp.Body.Close()
	if resp.StatusCode != 403 {
		t.Fatalf("plain HTTP: %d, want 403", resp.StatusCode)
	}
}

type denyAll struct{}

func (denyAll) Authenticate(string, string) bool { panic("credentials checked over plain HTTP") }

func TestRedirectToHTTPS(t *testing.T) {
	h := RedirectToHTTPS("4443")
	cases := map[string]string{
		"example.com:8080": "https://example.com:4443/task/1/?x=y",
		"example.com":      "https://example.com:4443/task/1/?x=y",
	}
	for host, want := range cases {
		r := httptest.NewRequest("GET", "/task/1/?x=y", nil)
		r.Host = host
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		if w.Code != http.StatusPermanentRedirect || w.Header().Get("Location") != want {
			t.Errorf("host %s: %d %q, want 308 %q", host, w.Code, w.Header().Get("Location"), want)
		}
	}
	r := httptest.NewRequest("GET", "/", bytes.NewReader(nil))
	r.Host = "evil.com/@x"
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	if w.Code != 400 {
		t.Errorf("odd host: %d, want 400", w.Code)
	}
	w = httptest.NewRecorder()
	r = httptest.NewRequest("GET", "/", nil)
	r.Host = "example.com"
	RedirectToHTTPS("443").ServeHTTP(w, r)
	if got := w.Header().Get("Location"); got != "https://example.com/" {
		t.Errorf("port 443 redirect = %q", got)
	}
}
