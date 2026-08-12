// Command tcpchat is a multi-client TCP chat server. This file is the only
// place that talks to net.Conn; everything it decides to do with a line of
// input is delegated to *chat.Hub, so the interesting logic lives in a
// package that never has to open a socket to be tested.
package main

import (
	"bufio"
	"fmt"
	"net"
	"strings"

	"tcpchat/internal/chat"
)

const maxNickAttempts = 3

// ListenAndServe starts accepting connections on addr and serves each one
// against hub in its own goroutine. It returns as soon as the listener is
// up; closing the returned net.Listener stops the accept loop.
func ListenAndServe(addr string, hub *chat.Hub) (net.Listener, error) {
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		return nil, err
	}
	go acceptLoop(ln, hub)
	return ln, nil
}

func acceptLoop(ln net.Listener, hub *chat.Hub) {
	for {
		conn, err := ln.Accept()
		if err != nil {
			return // listener closed
		}
		go handleConn(conn, hub)
	}
}

func handleConn(conn net.Conn, hub *chat.Hub) {
	defer conn.Close()

	client := chat.NewClient()
	writerDone := make(chan struct{})
	go writeLoop(conn, client, writerDone)

	// writeLoop is the only goroutine that writes to conn from here on;
	// closing client.Send tells it to finish draining and exit.
	defer func() {
		hub.Leave(client) // safe even if negotiation never finished
		close(client.Send)
		<-writerDone
	}()

	scanner := bufio.NewScanner(conn)
	if !negotiateNick(conn, scanner, hub, client) {
		return
	}

	for scanner.Scan() {
		line := strings.TrimRight(scanner.Text(), "\r\n")
		if line == "" {
			continue
		}
		if !dispatch(hub, client, line) {
			return
		}
	}
}

// negotiateNick prompts for a nickname and retries on rejection, up to
// maxNickAttempts, before giving up on the connection.
func negotiateNick(conn net.Conn, scanner *bufio.Scanner, hub *chat.Hub, client *chat.Client) bool {
	for attempt := 0; attempt < maxNickAttempts; attempt++ {
		fmt.Fprint(conn, "Nick: ")
		if !scanner.Scan() {
			return false
		}
		nick := strings.TrimSpace(scanner.Text())
		if err := hub.Join(client, nick); err != nil {
			fmt.Fprintf(conn, "error: %v\n", err)
			continue
		}
		return true
	}
	fmt.Fprintln(conn, "error: too many failed attempts, disconnecting")
	return false
}

// dispatch acts on one line of input from an already-joined client. It
// returns false when the connection should close.
func dispatch(hub *chat.Hub, client *chat.Client, line string) bool {
	if !strings.HasPrefix(line, "/") {
		hub.Broadcast(client, line)
		return true
	}

	fields := strings.Fields(line)
	switch fields[0] {
	case "/quit":
		return false

	case "/list":
		names := hub.ListNicks()
		trySend(client, fmt.Sprintf("* online (%d): %s", len(names), strings.Join(names, ", ")))

	case "/nick":
		if len(fields) != 2 {
			trySend(client, "error: usage: /nick <new-name>")
			break
		}
		if err := hub.Rename(client, fields[1]); err != nil {
			trySend(client, fmt.Sprintf("error: %v", err))
		}

	case "/msg":
		if len(fields) < 3 {
			trySend(client, "error: usage: /msg <nick> <message>")
			break
		}
		msg := strings.SplitN(line, " ", 3)[2]
		if err := hub.Whisper(client, fields[1], msg); err != nil {
			trySend(client, fmt.Sprintf("error: %v", err))
		}

	default:
		trySend(client, fmt.Sprintf("error: unknown command %q", fields[0]))
	}
	return true
}

func writeLoop(conn net.Conn, client *chat.Client, done chan struct{}) {
	defer close(done)
	for line := range client.Send {
		if _, err := fmt.Fprintln(conn, line); err != nil {
			return
		}
	}
}

// trySend delivers straight to a client's own connection (command replies,
// errors) without ever blocking the caller's goroutine.
func trySend(c *chat.Client, line string) {
	select {
	case c.Send <- line:
	default:
	}
}
