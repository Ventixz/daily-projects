// Command restauth is a task REST API served over HTTPS with HTTP Basic auth.
//
//	restauth gencert [-host localhost,127.0.0.1] [-out .]   write cert.pem and key.pem
//	restauth hashpw                                          read a password on stdin, print its hash
//	restauth serve [-addr :8443] [-http-addr :8080] [-users users.txt]
package main

import (
	"bufio"
	"crypto/tls"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: restauth gencert|hashpw|serve [flags]")
		os.Exit(2)
	}
	var err error
	switch os.Args[1] {
	case "gencert":
		err = cmdGencert(os.Args[2:])
	case "hashpw":
		err = cmdHashpw()
	case "serve":
		err = cmdServe(os.Args[2:])
	default:
		err = fmt.Errorf("unknown command %q", os.Args[1])
	}
	if err != nil {
		log.Fatal(err)
	}
}

func cmdGencert(args []string) error {
	fs := flag.NewFlagSet("gencert", flag.ExitOnError)
	hosts := fs.String("host", "localhost,127.0.0.1", "comma-separated DNS names / IPs")
	out := fs.String("out", ".", "output directory")
	days := fs.Int("days", 30, "validity in days")
	fs.Parse(args)
	certPEM, keyPEM, err := GenerateSelfSigned(strings.Split(*hosts, ","), time.Duration(*days)*24*time.Hour)
	if err != nil {
		return err
	}
	if err := os.WriteFile(filepath.Join(*out, "cert.pem"), certPEM, 0o644); err != nil {
		return err
	}
	// The private key is readable by the owner only.
	if err := os.WriteFile(filepath.Join(*out, "key.pem"), keyPEM, 0o600); err != nil {
		return err
	}
	fmt.Println("wrote cert.pem and key.pem")
	return nil
}

func cmdHashpw() error {
	line, err := bufio.NewReader(os.Stdin).ReadString('\n')
	if err != nil && line == "" {
		return errors.New("no password on stdin")
	}
	h, err := HashPassword(strings.TrimRight(line, "\r\n"), DefaultIterations)
	if err != nil {
		return err
	}
	fmt.Println(h)
	return nil
}

func cmdServe(args []string) error {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	addr := fs.String("addr", ":8443", "HTTPS listen address")
	httpAddr := fs.String("http-addr", ":8080", "plain-HTTP redirect address (empty to disable)")
	certFile := fs.String("cert", "cert.pem", "certificate PEM")
	keyFile := fs.String("key", "key.pem", "private key PEM")
	usersFile := fs.String("users", "users.txt", "file of username:hash lines")
	fs.Parse(args)

	cert, err := tls.LoadX509KeyPair(*certFile, *keyFile)
	if err != nil {
		return fmt.Errorf("load cert (run `restauth gencert` first?): %w", err)
	}
	f, err := os.Open(*usersFile)
	if err != nil {
		return err
	}
	hashes, err := ParseUsersFile(f)
	f.Close()
	if err != nil {
		return fmt.Errorf("%s: %w", *usersFile, err)
	}
	users, err := NewUserStore(hashes, DefaultIterations)
	if err != nil {
		return err
	}

	srv := NewHTTPSServer(*addr, HSTS(180*24*time.Hour, NewAPI(NewTaskStore(), users)), cert)
	if *httpAddr != "" {
		_, port, err := net.SplitHostPort(*addr)
		if err != nil {
			return err
		}
		go func() {
			r := &http.Server{Addr: *httpAddr, Handler: RedirectToHTTPS(port), ReadHeaderTimeout: 5 * time.Second}
			log.Printf("redirecting http://%s -> https", *httpAddr)
			log.Println(r.ListenAndServe())
		}()
	}
	log.Printf("serving https://%s (%d users)", *addr, len(hashes))
	// Cert and key are already in TLSConfig, so the file arguments are empty.
	return srv.ListenAndServeTLS("", "")
}
