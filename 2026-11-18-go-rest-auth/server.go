package main

import (
	"crypto/tls"
	"net"
	"net/http"
	"strconv"
	"time"
)

// TLSConfig returns the server's TLS settings: TLS 1.3 only. 1.3 removed the
// weak cipher suites and renegotiation, and Go picks its suites itself, so
// there is no cipher list to get wrong.
func TLSConfig(cert tls.Certificate) *tls.Config {
	return &tls.Config{
		MinVersion:   tls.VersionTLS13,
		Certificates: []tls.Certificate{cert},
	}
}

// NewHTTPSServer builds a server with timeouts set. The zero-value
// http.Server has none, so one slow client can hold a connection forever.
func NewHTTPSServer(addr string, h http.Handler, cert tls.Certificate) *http.Server {
	return &http.Server{
		Addr:              addr,
		Handler:           h,
		TLSConfig:         TLSConfig(cert),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       15 * time.Second,
		WriteTimeout:      15 * time.Second,
		IdleTimeout:       60 * time.Second,
	}
}

// RedirectToHTTPS answers plain-HTTP requests with a 308 to the same path on
// the HTTPS port. 308 (not 301) keeps the method and body, so a stray POST is
// redirected as a POST. Note the redirect itself can't protect a password that
// was already sent in plaintext; that's why BasicAuth also refuses non-TLS.
func RedirectToHTTPS(httpsPort string) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		host := r.Host
		if h, _, err := net.SplitHostPort(r.Host); err == nil {
			host = h
		}
		target := "https://" + net.JoinHostPort(host, httpsPort) + r.URL.RequestURI()
		if httpsPort == "443" {
			target = "https://" + host + r.URL.RequestURI()
		}
		http.Redirect(w, r, target, http.StatusPermanentRedirect)
	})
}

// HSTS tells browsers to use HTTPS for this host for maxAge, so after the
// first visit they never send a plaintext request (and its credentials) at all.
func HSTS(maxAge time.Duration, next http.Handler) http.Handler {
	value := "max-age=" + strconv.Itoa(int(maxAge.Seconds()))
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.TLS != nil { // browsers ignore it over HTTP anyway
			w.Header().Set("Strict-Transport-Security", value)
		}
		next.ServeHTTP(w, r)
	})
}
