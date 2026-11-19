// Command restauth runs the authenticated task server and its helper tools.
//
//	restauth gencert  -host localhost,127.0.0.1 -cert cert.pem -key key.pem
//	restauth hashpw   alice            (reads the password from stdin, prints "alice:<hash>")
//	restauth serve    -addr :4443 -cert cert.pem -key key.pem -users users.txt [-redirect :8080]
package main

import (
	"bufio"
	"context"
	"crypto/tls"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"restauth/internal/auth"
	"restauth/internal/password"
	"restauth/internal/server"
	"restauth/internal/taskstore"
	"restauth/internal/tlsutil"
)

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	var err error
	switch os.Args[1] {
	case "gencert":
		err = gencert(os.Args[2:])
	case "hashpw":
		err = hashpw(os.Args[2:])
	case "serve":
		err = serve(os.Args[2:])
	default:
		usage()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "restauth:", err)
		os.Exit(1)
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, "usage: restauth gencert|hashpw|serve [flags]")
	os.Exit(2)
}

func gencert(args []string) error {
	fs := flag.NewFlagSet("gencert", flag.ExitOnError)
	hosts := fs.String("host", "localhost,127.0.0.1,::1", "comma-separated host names / IPs")
	certPath := fs.String("cert", "cert.pem", "certificate output")
	keyPath := fs.String("key", "key.pem", "private key output")
	days := fs.Int("days", 30, "validity in days")
	fs.Parse(args)
	certPEM, keyPEM, err := tlsutil.SelfSigned(strings.Split(*hosts, ","), time.Duration(*days)*24*time.Hour)
	if err != nil {
		return err
	}
	if err := os.WriteFile(*certPath, certPEM, 0o644); err != nil {
		return err
	}
	// The key is the only secret here: owner-only permissions.
	if err := os.WriteFile(*keyPath, keyPEM, 0o600); err != nil {
		return err
	}
	fmt.Printf("wrote %s and %s\n", *certPath, *keyPath)
	return nil
}

func hashpw(args []string) error {
	fs := flag.NewFlagSet("hashpw", flag.ExitOnError)
	iters := fs.Int("iterations", password.DefaultIterations, "PBKDF2 iterations")
	fs.Parse(args)
	if fs.NArg() != 1 || strings.ContainsAny(fs.Arg(0), ": \t") {
		return errors.New("usage: restauth hashpw <username> (password on stdin)")
	}
	fmt.Fprint(os.Stderr, "password: ")
	line, err := bufio.NewReader(os.Stdin).ReadString('\n')
	if err != nil && line == "" {
		return err
	}
	pw := strings.TrimRight(line, "\r\n")
	if len(pw) < 8 {
		return errors.New("password must be at least 8 characters")
	}
	h, err := password.Hash(pw, *iters)
	if err != nil {
		return err
	}
	fmt.Printf("%s:%s\n", fs.Arg(0), h)
	return nil
}

func serve(args []string) error {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	addr := fs.String("addr", "127.0.0.1:4443", "HTTPS listen address")
	certPath := fs.String("cert", "cert.pem", "certificate file")
	keyPath := fs.String("key", "key.pem", "private key file")
	usersPath := fs.String("users", "users.txt", "users file (name:hash per line)")
	redirect := fs.String("redirect", "", "optional plain-HTTP address that redirects to HTTPS")
	fs.Parse(args)

	f, err := os.Open(*usersPath)
	if err != nil {
		return err
	}
	users, err := auth.LoadUsers(f)
	f.Close()
	if err != nil {
		return err
	}
	if users.Len() == 0 {
		return errors.New("users file has no users")
	}
	cert, err := tls.LoadX509KeyPair(*certPath, *keyPath)
	if err != nil {
		return err
	}

	srv := &http.Server{
		Addr:              *addr,
		Handler:           server.New(taskstore.New(), users, "tasks"),
		TLSConfig:         tlsutil.ServerConfig(cert),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       15 * time.Second,
		WriteTimeout:      15 * time.Second,
		IdleTimeout:       60 * time.Second,
		MaxHeaderBytes:    16 << 10,
	}
	var redirectSrv *http.Server
	if *redirect != "" {
		_, port, err := net.SplitHostPort(*addr)
		if err != nil {
			return err
		}
		redirectSrv = &http.Server{
			Addr: *redirect, Handler: server.RedirectToHTTPS(port),
			ReadHeaderTimeout: 5 * time.Second,
		}
		go func() {
			log.Printf("redirecting http://%s -> https", *redirect)
			if err := redirectSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
				log.Print(err)
			}
		}()
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	errc := make(chan error, 1)
	go func() {
		log.Printf("serving https://%s for %d users", *addr, users.Len())
		errc <- srv.ListenAndServeTLS("", "") // certs come from TLSConfig
	}()
	select {
	case err := <-errc:
		return err
	case <-ctx.Done():
	}
	shutdown, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if redirectSrv != nil {
		redirectSrv.Shutdown(shutdown)
	}
	return srv.Shutdown(shutdown)
}
