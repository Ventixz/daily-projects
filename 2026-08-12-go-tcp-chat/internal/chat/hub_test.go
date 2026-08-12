package chat

import (
	"fmt"
	"sort"
	"strings"
	"sync"
	"testing"
)

// drain reads every line currently buffered on c.Send without blocking.
func drain(t *testing.T, c *Client) []string {
	t.Helper()
	var lines []string
	for {
		select {
		case line := <-c.Send:
			lines = append(lines, line)
		default:
			return lines
		}
	}
}

func mustJoin(t *testing.T, h *Hub, nick string) *Client {
	t.Helper()
	c := NewClient()
	if err := h.Join(c, nick); err != nil {
		t.Fatalf("Join(%q): unexpected error %v", nick, err)
	}
	drain(t, c) // discard the self-join announcement, tests assert on it explicitly where relevant
	return c
}

// joinAll joins every nick in order, then drains every resulting client's
// buffer. Joining alice then bob delivers bob's arrival to alice too, so
// draining each client only as it joins (mustJoin's approach) leaves that
// announcement sitting unread in alice's buffer; joinAll settles the whole
// group at once so callers start from a clean slate for every client.
func joinAll(t *testing.T, h *Hub, nicks ...string) []*Client {
	t.Helper()
	clients := make([]*Client, 0, len(nicks))
	for _, nick := range nicks {
		c := NewClient()
		if err := h.Join(c, nick); err != nil {
			t.Fatalf("Join(%q): unexpected error %v", nick, err)
		}
		clients = append(clients, c)
	}
	for _, c := range clients {
		drain(t, c)
	}
	return clients
}

func TestJoinAnnouncesArrivalToEveryoneIncludingSelf(t *testing.T) {
	h := NewHub()
	alice := NewClient()
	if err := h.Join(alice, "alice"); err != nil {
		t.Fatalf("Join: %v", err)
	}
	got := drain(t, alice)
	want := "* alice has joined (1 online)"
	if len(got) != 1 || got[0] != want {
		t.Fatalf("alice's own join announcement = %v, want [%q]", got, want)
	}

	bob := NewClient()
	if err := h.Join(bob, "bob"); err != nil {
		t.Fatalf("Join: %v", err)
	}
	if got := drain(t, alice); len(got) != 1 || got[0] != "* bob has joined (2 online)" {
		t.Fatalf("alice heard about bob joining = %v", got)
	}
}

func TestJoinRejectsDuplicateNick(t *testing.T) {
	h := NewHub()
	mustJoin(t, h, "alice")

	dupe := NewClient()
	err := h.Join(dupe, "alice")
	if err != ErrNickTaken {
		t.Fatalf("Join with taken nick: got %v, want ErrNickTaken", err)
	}
	if len(h.ListNicks()) != 1 {
		t.Fatalf("rejected joiner should not be registered, ListNicks = %v", h.ListNicks())
	}
}

func TestJoinRejectsInvalidNicks(t *testing.T) {
	cases := []string{"", "has space", strings.Repeat("x", 21), "tab\tin\tit"}
	for _, nick := range cases {
		h := NewHub()
		c := NewClient()
		if err := h.Join(c, nick); err != ErrNickInvalid {
			t.Errorf("Join(%q): got %v, want ErrNickInvalid", nick, err)
		}
	}
}

func TestLeaveAnnouncesToOthersButNotSelf(t *testing.T) {
	h := NewHub()
	clients := joinAll(t, h, "alice", "bob")
	alice, bob := clients[0], clients[1]

	h.Leave(alice)

	if got := drain(t, alice); len(got) != 0 {
		t.Fatalf("a client that left should get nothing further, got %v", got)
	}
	want := "* alice has left (1 online)"
	if got := drain(t, bob); len(got) != 1 || got[0] != want {
		t.Fatalf("bob heard about alice leaving = %v, want [%q]", got, want)
	}
	if names := h.ListNicks(); len(names) != 1 || names[0] != "bob" {
		t.Fatalf("ListNicks after leave = %v, want [bob]", names)
	}
}

func TestLeaveIsNoOpForUnknownClient(t *testing.T) {
	h := NewHub()
	stranger := NewClient() // never joined
	h.Leave(stranger)       // must not panic on the missing map entries
	if len(h.ListNicks()) != 0 {
		t.Fatalf("Leave of a stranger should not touch the roster")
	}
}

func TestRenameUpdatesRosterAndAnnounces(t *testing.T) {
	h := NewHub()
	alice := mustJoin(t, h, "alice")
	bob := mustJoin(t, h, "bob")

	if err := h.Rename(alice, "alicia"); err != nil {
		t.Fatalf("Rename: %v", err)
	}
	if alice.Nick != "alicia" {
		t.Fatalf("Rename did not update Client.Nick, got %q", alice.Nick)
	}
	want := "* alice is now known as alicia"
	if got := drain(t, bob); len(got) != 1 || got[0] != want {
		t.Fatalf("bob heard about the rename = %v, want [%q]", got, want)
	}

	names := h.ListNicks()
	sort.Strings(names)
	if fmt.Sprint(names) != "[alicia bob]" {
		t.Fatalf("ListNicks after rename = %v", names)
	}
}

func TestRenameConflictLeavesOldNickIntact(t *testing.T) {
	h := NewHub()
	alice := mustJoin(t, h, "alice")
	mustJoin(t, h, "bob")

	err := h.Rename(alice, "bob")
	if err != ErrNickTaken {
		t.Fatalf("Rename to taken nick: got %v, want ErrNickTaken", err)
	}
	if alice.Nick != "alice" {
		t.Fatalf("failed rename must not change Client.Nick, got %q", alice.Nick)
	}
	// A message addressed to "alice" must still reach her under the old nick.
	if err := h.Whisper(mustJoin(t, h, "carol"), "alice", "hi"); err != nil {
		t.Fatalf("Whisper to alice after failed rename: %v", err)
	}
}

func TestRenameToOwnCurrentNickIsNoOp(t *testing.T) {
	h := NewHub()
	alice := mustJoin(t, h, "alice")
	if err := h.Rename(alice, "alice"); err != nil {
		t.Fatalf("Rename to same nick: %v", err)
	}
	if names := h.ListNicks(); len(names) != 1 || names[0] != "alice" {
		t.Fatalf("ListNicks after self-rename = %v", names)
	}
}

func TestBroadcastReachesOthersNotSender(t *testing.T) {
	h := NewHub()
	clients := joinAll(t, h, "alice", "bob", "carol")
	alice, bob, carol := clients[0], clients[1], clients[2]

	h.Broadcast(alice, "hello room")

	if got := drain(t, alice); len(got) != 0 {
		t.Fatalf("sender should not receive its own broadcast, got %v", got)
	}
	want := "alice: hello room"
	for name, c := range map[string]*Client{"bob": bob, "carol": carol} {
		if got := drain(t, c); len(got) != 1 || got[0] != want {
			t.Errorf("%s received %v, want [%q]", name, got, want)
		}
	}
}

func TestBroadcastDropsRatherThanBlocksWhenBufferIsFull(t *testing.T) {
	h := NewHub()
	alice := mustJoin(t, h, "alice")
	slow := mustJoin(t, h, "slow")

	// Fill slow's buffer completely without anything draining it.
	for i := 0; i < sendBuffer; i++ {
		h.Broadcast(alice, fmt.Sprintf("msg %d", i))
	}
	if len(slow.Send) != sendBuffer {
		t.Fatalf("buffer should be full, len = %d, want %d", len(slow.Send), sendBuffer)
	}

	// One more must not block (this call would hang the test on a broken
	// implementation) and must not grow the channel past its capacity.
	h.Broadcast(alice, "one too many")
	if len(slow.Send) != sendBuffer {
		t.Fatalf("overflow broadcast changed buffer length to %d, want unchanged %d", len(slow.Send), sendBuffer)
	}
}

func TestWhisperDeliversToRecipientAndEchoesToSender(t *testing.T) {
	h := NewHub()
	clients := joinAll(t, h, "alice", "bob", "carol")
	alice, bob, carol := clients[0], clients[1], clients[2]

	if err := h.Whisper(alice, "bob", "secret"); err != nil {
		t.Fatalf("Whisper: %v", err)
	}
	if got := drain(t, bob); len(got) != 1 || got[0] != "[alice -> you]: secret" {
		t.Fatalf("bob received %v", got)
	}
	if got := drain(t, alice); len(got) != 1 || got[0] != "[you -> bob]: secret" {
		t.Fatalf("alice's echo = %v", got)
	}
	if got := drain(t, carol); len(got) != 0 {
		t.Fatalf("carol should not see a whisper meant for bob, got %v", got)
	}
}

func TestWhisperToUnknownNickErrors(t *testing.T) {
	h := NewHub()
	alice := mustJoin(t, h, "alice")
	if err := h.Whisper(alice, "ghost", "hi"); err != ErrNoSuchNick {
		t.Fatalf("Whisper to unknown nick: got %v, want ErrNoSuchNick", err)
	}
}

func TestWhisperToSelfErrors(t *testing.T) {
	h := NewHub()
	alice := mustJoin(t, h, "alice")
	if err := h.Whisper(alice, "alice", "hi"); err != ErrSelfWhisper {
		t.Fatalf("Whisper to self: got %v, want ErrSelfWhisper", err)
	}
}

func TestListNicksIsSorted(t *testing.T) {
	h := NewHub()
	mustJoin(t, h, "carol")
	mustJoin(t, h, "alice")
	mustJoin(t, h, "bob")

	names := h.ListNicks()
	if fmt.Sprint(names) != "[alice bob carol]" {
		t.Fatalf("ListNicks = %v, want sorted [alice bob carol]", names)
	}
}

// TestConcurrentJoinBroadcastLeaveIsRaceFree exercises Join/Broadcast/Leave
// from many goroutines at once. It doesn't assert on message content --
// that's covered above with a single goroutine driving the Hub -- it
// exists to be run under `go test -race`, where a missing lock in Hub
// shows up as a data race on the client maps rather than as a wrong
// answer.
func TestConcurrentJoinBroadcastLeaveIsRaceFree(t *testing.T) {
	h := NewHub()
	const n = 50
	var wg sync.WaitGroup
	wg.Add(n)
	for i := 0; i < n; i++ {
		go func(i int) {
			defer wg.Done()
			c := NewClient()
			nick := fmt.Sprintf("user%d", i)
			if err := h.Join(c, nick); err != nil {
				t.Errorf("Join(%s): %v", nick, err)
				return
			}
			for j := 0; j < 10; j++ {
				h.Broadcast(c, "hi")
				drain(t, c)
			}
			h.Leave(c)
		}(i)
	}
	wg.Wait()

	if len(h.ListNicks()) != 0 {
		t.Fatalf("everyone left, ListNicks = %v, want empty", h.ListNicks())
	}
}
