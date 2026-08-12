package main

import (
	"flag"
	"log"

	"tcpchat/internal/chat"
)

func main() {
	addr := flag.String("addr", ":6000", "address to listen on")
	flag.Parse()

	hub := chat.NewHub()
	ln, err := ListenAndServe(*addr, hub)
	if err != nil {
		log.Fatalf("listen on %s: %v", *addr, err)
	}
	log.Printf("tcp chat listening on %s", ln.Addr())

	select {} // run until killed
}
