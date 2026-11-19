package tlsutil

import (
	"crypto/ecdsa"
	"crypto/tls"
	"crypto/x509"
	"encoding/pem"
	"testing"
	"time"
)

func TestSelfSigned(t *testing.T) {
	certPEM, keyPEM, err := SelfSigned([]string{"localhost", "127.0.0.1"}, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := tls.X509KeyPair(certPEM, keyPEM); err != nil {
		t.Fatalf("cert and key don't pair: %v", err)
	}
	block, _ := pem.Decode(certPEM)
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := cert.PublicKey.(*ecdsa.PublicKey); !ok {
		t.Fatalf("public key is %T, want ECDSA", cert.PublicKey)
	}
	if err := cert.VerifyHostname("localhost"); err != nil {
		t.Error(err)
	}
	if err := cert.VerifyHostname("127.0.0.1"); err != nil {
		t.Error(err)
	}
	if err := cert.VerifyHostname("example.com"); err == nil {
		t.Error("cert valid for a host it wasn't issued for")
	}
	if d := cert.NotAfter.Sub(time.Now()); d > time.Hour || d < 50*time.Minute {
		t.Errorf("validity %v, want ~1h", d)
	}
	pool := x509.NewCertPool()
	pool.AddCert(cert)
	if _, err := cert.Verify(x509.VerifyOptions{Roots: pool, DNSName: "localhost"}); err != nil {
		t.Errorf("cert doesn't verify against itself as root: %v", err)
	}
}
