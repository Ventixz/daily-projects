package main

import (
	"crypto/tls"
	"crypto/x509"
	"encoding/pem"
	"errors"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

const testIter = 1_000 // fast for tests; production uses DefaultIterations

func mustUsers(t *testing.T) *UserStore {
	t.Helper()
	hashes := map[string]string{}
	for name, pw := range map[string]string{"alice": "wonderland", "bob": "builder"} {
		h, err := HashPassword(pw, testIter)
		if err != nil {
			t.Fatal(err)
		}
		hashes[name] = h
	}
	us, err := NewUserStore(hashes, testIter)
	if err != nil {
		t.Fatal(err)
	}
	return us
}

// ---- passwords ----

func TestHashAndVerify(t *testing.T) {
	h, err := HashPassword("s3cret", testIter)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(h, "pbkdf2-sha256$1000$") {
		t.Fatalf("unexpected format %q", h)
	}
	if ok, err := VerifyPassword("s3cret", h); !ok || err != nil {
		t.Fatalf("correct password rejected: %v %v", ok, err)
	}
	if ok, _ := VerifyPassword("s3creT", h); ok {
		t.Fatal("wrong password accepted")
	}
}

func TestSaltMakesHashesDiffer(t *testing.T) {
	a, _ := HashPassword("same", testIter)
	b, _ := HashPassword("same", testIter)
	if a == b {
		t.Fatal("two hashes of the same password are identical: salt not random")
	}
}

func TestMalformedHashes(t *testing.T) {
	for _, h := range []string{"", "plain", "md5$1000$AA$AA", "pbkdf2-sha256$10$AAAA$AAAA",
		"pbkdf2-sha256$x$AAAA$AAAA", "pbkdf2-sha256$1000$!!$AAAA", "pbkdf2-sha256$1000$AAAA"} {
		if _, err := VerifyPassword("x", h); !errors.Is(err, ErrMalformedHash) {
			t.Errorf("%q: want ErrMalformedHash, got %v", h, err)
		}
	}
	if _, err := NewUserStore(map[string]string{"eve": "nope"}, testIter); err == nil {
		t.Error("NewUserStore accepted a malformed hash")
	}
}

func TestUserStoreAuthenticate(t *testing.T) {
	us := mustUsers(t)
	cases := []struct {
		user, pw string
		want     bool
	}{
		{"alice", "wonderland", true},
		{"alice", "builder", false}, // bob's password
		{"mallory", "wonderland", false},
		{"", "", false},
	}
	for _, c := range cases {
		if got := us.Authenticate(c.user, c.pw); got != c.want {
			t.Errorf("Authenticate(%q,%q)=%v want %v", c.user, c.pw, got, c.want)
		}
	}
}

// An unknown username must cost a full key derivation too; otherwise a fast
// 401 tells an attacker the user doesn't exist. Uses a high iteration count so
// the KDF dominates and the ratio is stable.
func TestUnknownUserTakesAsLongAsWrongPassword(t *testing.T) {
	const iter = 200_000
	h, _ := HashPassword("pw", iter)
	us, err := NewUserStore(map[string]string{"alice": h}, iter)
	if err != nil {
		t.Fatal(err)
	}
	timeIt := func(user string) time.Duration {
		best := time.Hour
		for i := 0; i < 3; i++ {
			start := time.Now()
			us.Authenticate(user, "wrong")
			best = min(best, time.Since(start))
		}
		return best
	}
	known, unknown := timeIt("alice"), timeIt("nobody")
	if unknown < known/2 {
		t.Fatalf("unknown user %v vs known user %v: user existence leaks via timing", unknown, known)
	}
}

func TestParseUsersFile(t *testing.T) {
	got, err := ParseUsersFile(strings.NewReader("# comment\n\nalice:h1\n bob:h2 \n"))
	if err != nil || len(got) != 2 || got["alice"] != "h1" || got["bob"] != "h2" {
		t.Fatalf("got %v, %v", got, err)
	}
	for _, bad := range []string{"nocolon\n", ":hash\n", "a:h\na:h\n"} {
		if _, err := ParseUsersFile(strings.NewReader(bad)); err == nil {
			t.Errorf("%q: expected error", bad)
		}
	}
}

// ---- certificates ----

func TestGenerateSelfSigned(t *testing.T) {
	certPEM, keyPEM, err := GenerateSelfSigned([]string{"localhost", "127.0.0.1"}, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := tls.X509KeyPair(certPEM, keyPEM); err != nil {
		t.Fatalf("cert and key don't match: %v", err)
	}
	block, _ := pem.Decode(certPEM)
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		t.Fatal(err)
	}
	if len(cert.DNSNames) != 1 || cert.DNSNames[0] != "localhost" ||
		len(cert.IPAddresses) != 1 || !cert.IPAddresses[0].Equal(net.ParseIP("127.0.0.1")) {
		t.Fatalf("SANs wrong: %v %v", cert.DNSNames, cert.IPAddresses)
	}
	pool := x509.NewCertPool()
	pool.AddCert(cert)
	for host, wantOK := range map[string]bool{"localhost": true, "127.0.0.1": true, "example.com": false} {
		_, err := cert.Verify(x509.VerifyOptions{DNSName: host, Roots: pool})
		if (err == nil) != wantOK {
			t.Errorf("verify for %q: err=%v, want ok=%v", host, err, wantOK)
		}
	}
	if _, _, err := GenerateSelfSigned(nil, time.Hour); err == nil {
		t.Error("expected error for no hosts")
	}
}

// ---- middleware (no network) ----

func TestBasicAuthMiddleware(t *testing.T) {
	us := mustUsers(t)
	h := BasicAuth(us, "tasks", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		u, _ := UserFromContext(r.Context())
		io.WriteString(w, u)
	}))
	do := func(tlsOn bool, setAuth func(*http.Request)) *httptest.ResponseRecorder {
		r := httptest.NewRequest("GET", "/", nil)
		if !tlsOn {
			r.TLS = nil
		} else {
			r.TLS = &tls.ConnectionState{}
		}
		if setAuth != nil {
			setAuth(r)
		}
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		return w
	}

	if w := do(true, nil); w.Code != 401 || w.Header().Get("WWW-Authenticate") != `Basic realm="tasks", charset="UTF-8"` {
		t.Fatalf("no creds: %d %q", w.Code, w.Header().Get("WWW-Authenticate"))
	}
	if w := do(true, func(r *http.Request) { r.SetBasicAuth("alice", "nope") }); w.Code != 401 {
		t.Fatalf("wrong pw: %d", w.Code)
	}
	if w := do(true, func(r *http.Request) { r.Header.Set("Authorization", "Bearer abc") }); w.Code != 401 {
		t.Fatalf("wrong scheme: %d", w.Code)
	}
	if w := do(false, func(r *http.Request) { r.SetBasicAuth("alice", "wonderland") }); w.Code != 403 {
		t.Fatalf("plaintext request should be refused, got %d", w.Code)
	}
	if w := do(true, func(r *http.Request) { r.SetBasicAuth("alice", "wonderland") }); w.Code != 200 || w.Body.String() != "alice" {
		t.Fatalf("good creds: %d %q", w.Code, w.Body.String())
	}
}

func TestRedirectToHTTPS(t *testing.T) {
	cases := map[string]string{
		"8443": "https://example.com:8443/task/3?x=1",
		"443":  "https://example.com/task/3?x=1",
	}
	for port, want := range cases {
		r := httptest.NewRequest("POST", "http://example.com:8080/task/3?x=1", nil)
		w := httptest.NewRecorder()
		RedirectToHTTPS(port).ServeHTTP(w, r)
		if w.Code != http.StatusPermanentRedirect || w.Header().Get("Location") != want {
			t.Errorf("port %s: %d %q, want 308 %q", port, w.Code, w.Header().Get("Location"), want)
		}
	}
}

// ---- end to end over real TLS ----

// startTLS runs the API on a loopback listener using a freshly generated
// self-signed cert and our TLSConfig, and returns its URL plus a client that
// trusts exactly that cert.
func startTLS(t *testing.T) (string, *http.Client, []byte) {
	t.Helper()
	certPEM, keyPEM, err := GenerateSelfSigned([]string{"127.0.0.1"}, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	cert, err := tls.X509KeyPair(certPEM, keyPEM)
	if err != nil {
		t.Fatal(err)
	}
	srv := httptest.NewUnstartedServer(HSTS(time.Hour, NewAPI(NewTaskStore(), mustUsers(t))))
	srv.TLS = TLSConfig(cert)
	srv.StartTLS()
	t.Cleanup(srv.Close)

	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(certPEM) {
		t.Fatal("bad PEM")
	}
	client := &http.Client{Transport: &http.Transport{TLSClientConfig: &tls.Config{RootCAs: pool}}}
	return srv.URL, client, certPEM
}

func req(t *testing.T, c *http.Client, method, url, body, user, pw string) (*http.Response, string) {
	t.Helper()
	r, _ := http.NewRequest(method, url, strings.NewReader(body))
	if body != "" {
		r.Header.Set("Content-Type", "application/json")
	}
	if user != "" {
		r.SetBasicAuth(user, pw)
	}
	resp, err := c.Do(r)
	if err != nil {
		t.Fatalf("%s %s: %v", method, url, err)
	}
	b, _ := io.ReadAll(resp.Body)
	resp.Body.Close()
	return resp, string(b)
}

func TestEndToEndTasks(t *testing.T) {
	url, c, _ := startTLS(t)

	if resp, _ := req(t, c, "POST", url+"/task/", `{"text":"x"}`, "", ""); resp.StatusCode != 401 {
		t.Fatalf("anonymous POST: %d", resp.StatusCode)
	}
	resp, body := req(t, c, "POST", url+"/task/", `{"text":"buy milk","tags":["home"]}`, "alice", "wonderland")
	if resp.StatusCode != 201 || resp.Header.Get("Location") != "/task/1" || !strings.Contains(body, `"owner":"alice"`) {
		t.Fatalf("create: %d %q %s", resp.StatusCode, resp.Header.Get("Location"), body)
	}
	if got := resp.Header.Get("Strict-Transport-Security"); got != "max-age=3600" {
		t.Fatalf("HSTS header = %q", got)
	}
	if resp.TLS == nil || resp.TLS.Version != tls.VersionTLS13 {
		t.Fatalf("expected TLS 1.3, got %+v", resp.TLS)
	}
	if resp, body := req(t, c, "GET", url+"/task/1", "", "", ""); resp.StatusCode != 200 || !strings.Contains(body, "buy milk") {
		t.Fatalf("public read: %d %s", resp.StatusCode, body)
	}
	if resp, body := req(t, c, "GET", url+"/whoami", "", "bob", "builder"); resp.StatusCode != 200 || !strings.Contains(body, `"bob"`) {
		t.Fatalf("whoami: %d %s", resp.StatusCode, body)
	}
	if resp, _ := req(t, c, "POST", url+"/task/", `{"text":"x","bogus":1}`, "alice", "wonderland"); resp.StatusCode != 400 {
		t.Fatalf("unknown field: %d", resp.StatusCode)
	}
	if resp, _ := req(t, c, "DELETE", url+"/task/1", "", "bob", "builder"); resp.StatusCode != 403 {
		t.Fatalf("bob deleting alice's task: %d", resp.StatusCode)
	}
	if resp, _ := req(t, c, "DELETE", url+"/task/1", "", "alice", "wonderland"); resp.StatusCode != 204 {
		t.Fatalf("owner delete: %d", resp.StatusCode)
	}
	if resp, _ := req(t, c, "DELETE", url+"/task/1", "", "alice", "wonderland"); resp.StatusCode != 404 {
		t.Fatalf("second delete: %d", resp.StatusCode)
	}
}

func TestClientMustTrustCert(t *testing.T) {
	url, _, _ := startTLS(t)
	_, err := http.Get(url + "/task/") // system roots don't include our self-signed cert
	var unknown x509.UnknownAuthorityError
	if !errors.As(err, &unknown) {
		t.Fatalf("expected UnknownAuthorityError, got %v", err)
	}
}

func TestOldTLSRejected(t *testing.T) {
	url, _, certPEM := startTLS(t)
	pool := x509.NewCertPool()
	pool.AppendCertsFromPEM(certPEM)
	old := &http.Client{Transport: &http.Transport{TLSClientConfig: &tls.Config{
		RootCAs: pool, MaxVersion: tls.VersionTLS12,
	}}}
	if _, err := old.Get(url + "/task/"); err == nil {
		t.Fatal("TLS 1.2 client was accepted")
	}
}
