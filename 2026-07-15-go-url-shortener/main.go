package main

import (
	"flag"
	"log"
	"net/http"

	"url-shortener/internal/shortener"
)

func main() {
	addr := flag.String("addr", ":8080", "address to listen on")
	dataFile := flag.String("data", "data/links.json", "path to the JSON persistence file")
	staticDir := flag.String("static", "static", "path to the static assets directory")
	flag.Parse()

	store, err := shortener.NewStore(*dataFile)
	if err != nil {
		log.Fatalf("loading store: %v", err)
	}

	log.Printf("listening on %s (data: %s)", *addr, *dataFile)
	if err := http.ListenAndServe(*addr, newServer(store, *staticDir)); err != nil {
		log.Fatal(err)
	}
}
