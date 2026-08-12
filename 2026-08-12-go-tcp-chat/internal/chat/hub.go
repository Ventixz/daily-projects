// Package chat holds the chat server's connection-independent state: who is
// online, under what nickname, and who should receive which line. Nothing
// in this package touches net.Conn or bufio — that lives in server.go, one
// level up, so this package can be unit tested without opening a socket.
package chat

import (
	"errors"
	"fmt"
	"sort"
	"sync"
)

var (
	ErrNickTaken   = errors.New("nickname is already taken")
	ErrNickInvalid = errors.New("nickname must be 1-20 characters with no whitespace")
	ErrNoSuchNick  = errors.New("no such user")
	ErrSelfWhisper = errors.New("cannot whisper yourself")
)

// sendBuffer bounds how many lines a client can be behind before Broadcast
// starts dropping messages for it rather than blocking every other client.
const sendBuffer = 16

// Client is one connected participant. The server owns the net.Conn and a
// writer goroutine that drains Send; Hub only ever touches Nick and Send.
type Client struct {
	Nick string
	Send chan string
}

func NewClient() *Client {
	return &Client{Send: make(chan string, sendBuffer)}
}

// Hub owns the set of connected clients. Every exported method takes h.mu,
// so any number of connection goroutines can call Join/Leave/Rename/
// Broadcast/Whisper concurrently without racing on the client maps.
type Hub struct {
	mu      sync.Mutex
	clients map[*Client]bool
	byNick  map[string]*Client
}

func NewHub() *Hub {
	return &Hub{
		clients: make(map[*Client]bool),
		byNick:  make(map[string]*Client),
	}
}

func validNick(nick string) bool {
	if len(nick) == 0 || len(nick) > 20 {
		return false
	}
	for _, r := range nick {
		if r == ' ' || r == '\t' || r == '\n' || r == '\r' {
			return false
		}
	}
	return true
}

// Join registers c under nick and announces the arrival to everyone now
// present, including c itself (that announcement doubles as c's join
// confirmation). On error c is left unregistered.
func (h *Hub) Join(c *Client, nick string) error {
	if !validNick(nick) {
		return ErrNickInvalid
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	if _, taken := h.byNick[nick]; taken {
		return ErrNickTaken
	}
	c.Nick = nick
	h.clients[c] = true
	h.byNick[nick] = c
	h.announceLocked(fmt.Sprintf("* %s has joined (%d online)", nick, len(h.clients)))
	return nil
}

// Leave removes c and announces the departure to everyone still present.
// c itself does not receive the announcement, since it's already gone.
// Leaving a client that was never joined (or already left) is a no-op.
func (h *Hub) Leave(c *Client) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if !h.clients[c] {
		return
	}
	delete(h.clients, c)
	delete(h.byNick, c.Nick)
	h.announceLocked(fmt.Sprintf("* %s has left (%d online)", c.Nick, len(h.clients)))
}

// Rename changes c's nickname, provided the new one is valid and not held
// by a different client. Renaming to the client's own current nick is a
// no-op success, not a conflict.
func (h *Hub) Rename(c *Client, newNick string) error {
	if !validNick(newNick) {
		return ErrNickInvalid
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	if existing, taken := h.byNick[newNick]; taken && existing != c {
		return ErrNickTaken
	}
	old := c.Nick
	if old == newNick {
		return nil
	}
	delete(h.byNick, old)
	c.Nick = newNick
	h.byNick[newNick] = c
	h.announceLocked(fmt.Sprintf("* %s is now known as %s", old, newNick))
	return nil
}

// Broadcast delivers msg, prefixed with from's nickname, to every other
// connected client. from does not get its own message echoed back.
func (h *Hub) Broadcast(from *Client, msg string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	line := fmt.Sprintf("%s: %s", from.Nick, msg)
	for c := range h.clients {
		if c == from {
			continue
		}
		h.deliverLocked(c, line)
	}
}

// Whisper delivers a private line to toNick and echoes a copy back to
// from, so the sender has confirmation of exactly what was sent.
func (h *Hub) Whisper(from *Client, toNick, msg string) error {
	h.mu.Lock()
	defer h.mu.Unlock()
	if toNick == from.Nick {
		return ErrSelfWhisper
	}
	to, ok := h.byNick[toNick]
	if !ok {
		return ErrNoSuchNick
	}
	h.deliverLocked(to, fmt.Sprintf("[%s -> you]: %s", from.Nick, msg))
	h.deliverLocked(from, fmt.Sprintf("[you -> %s]: %s", toNick, msg))
	return nil
}

// ListNicks returns the nicknames currently online, sorted for a stable,
// testable order.
func (h *Hub) ListNicks() []string {
	h.mu.Lock()
	defer h.mu.Unlock()
	names := make([]string, 0, len(h.clients))
	for c := range h.clients {
		names = append(names, c.Nick)
	}
	sort.Strings(names)
	return names
}

func (h *Hub) announceLocked(line string) {
	for c := range h.clients {
		h.deliverLocked(c, line)
	}
}

// deliverLocked pushes line onto c.Send without ever blocking. A client
// whose writer goroutine has fallen behind (or hung, or is gone) just
// misses the line — it does not stall the hub, and it does not stall
// every other client waiting behind it in the same Broadcast call.
func (h *Hub) deliverLocked(c *Client, line string) {
	select {
	case c.Send <- line:
	default:
	}
}
