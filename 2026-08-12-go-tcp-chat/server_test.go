package main

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"strings"
	"testing"
	"time"

	"tcpchat/internal/chat"
)

// startTestServer boots a real server on an OS-assigned loopback port and
// arranges for it to be closed when the test ends.
func startTestServer(t *testing.T) string {
	t.Helper()
	ln, err := ListenAndServe("127.0.0.1:0", chat.NewHub())
	if err != nil {
		t.Fatalf("ListenAndServe: %v", err)
	}
	t.Cleanup(func() { ln.Close() })
	return ln.Addr().String()
}

type testClient struct {
	t    *testing.T
	conn net.Conn
	r    *bufio.Reader
}

func dial(t *testing.T, addr string) *testClient {
	t.Helper()
	conn, err := net.Dial("tcp", addr)
	if err != nil {
		t.Fatalf("Dial: %v", err)
	}
	t.Cleanup(func() { conn.Close() })
	return &testClient{t: t, conn: conn, r: bufio.NewReader(conn)}
}

func (c *testClient) send(line string) {
	c.t.Helper()
	if _, err := fmt.Fprintln(c.conn, line); err != nil {
		c.t.Fatalf("send(%q): %v", line, err)
	}
}

// readLine reads up to the next newline and strips the unterminated
// "Nick: " prompt that the server may have written immediately before it
// -- from the client's side of a byte stream, the prompt and the next
// real line can arrive as one read even though the server wrote them in
// two separate calls.
func (c *testClient) readLine() string {
	c.t.Helper()
	c.conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	line, err := c.r.ReadString('\n')
	if err != nil {
		c.t.Fatalf("readLine: %v", err)
	}
	line = strings.TrimRight(line, "\r\n")
	return strings.TrimPrefix(line, "Nick: ")
}

// expectEOF asserts the server closed the connection.
func (c *testClient) expectEOF() {
	c.t.Helper()
	c.conn.SetReadDeadline(time.Now().Add(2 * time.Second))
	_, err := c.r.ReadByte()
	if err != io.EOF {
		c.t.Fatalf("expected EOF, got err=%v", err)
	}
}

// joinAs sends nick as the response to the (implicit) prompt and returns
// the resulting line -- the join announcement on success, or an "error: "
// line the caller can retry against.
func (c *testClient) joinAs(nick string) string {
	c.t.Helper()
	c.send(nick)
	return c.readLine()
}

func TestTwoClientsCanChat(t *testing.T) {
	addr := startTestServer(t)
	alice := dial(t, addr)
	bob := dial(t, addr)

	if got := alice.joinAs("alice"); got != "* alice has joined (1 online)" {
		t.Fatalf("alice join = %q", got)
	}
	if got := bob.joinAs("bob"); got != "* bob has joined (2 online)" {
		t.Fatalf("bob join = %q", got)
	}
	if got := alice.readLine(); got != "* bob has joined (2 online)" {
		t.Fatalf("alice should also hear about bob joining, got %q", got)
	}

	alice.send("hello bob")
	if got := bob.readLine(); got != "alice: hello bob" {
		t.Fatalf("bob received %q, want alice's message", got)
	}

	bob.send("hi alice")
	if got := alice.readLine(); got != "bob: hi alice" {
		t.Fatalf("alice received %q, want bob's message", got)
	}
}

func TestDuplicateNickIsRejectedThenRetrySucceeds(t *testing.T) {
	addr := startTestServer(t)
	alice := dial(t, addr)
	if got := alice.joinAs("alice"); got != "* alice has joined (1 online)" {
		t.Fatalf("alice join = %q", got)
	}

	impostor := dial(t, addr)
	if got := impostor.joinAs("alice"); !strings.Contains(got, "already taken") {
		t.Fatalf("impostor join = %q, want a taken-nick error", got)
	}
	if got := impostor.joinAs("alice2"); got != "* alice2 has joined (2 online)" {
		t.Fatalf("impostor retry join = %q", got)
	}
}

func TestListCommand(t *testing.T) {
	addr := startTestServer(t)
	alice := dial(t, addr)
	bob := dial(t, addr)
	alice.joinAs("alice")
	bob.joinAs("bob")
	alice.readLine() // bob's join announcement

	alice.send("/list")
	got := alice.readLine()
	if got != "* online (2): alice, bob" {
		t.Fatalf("/list = %q", got)
	}
}

func TestNickCommandRenamesAndIsVisibleToOthers(t *testing.T) {
	addr := startTestServer(t)
	alice := dial(t, addr)
	bob := dial(t, addr)
	alice.joinAs("alice")
	bob.joinAs("bob")
	alice.readLine() // bob's join announcement

	alice.send("/nick alicia")
	if got := alice.readLine(); got != "* alice is now known as alicia" {
		t.Fatalf("alice's own rename notice = %q", got)
	}
	if got := bob.readLine(); got != "* alice is now known as alicia" {
		t.Fatalf("bob should see the rename too, got %q", got)
	}

	bob.send("hi alicia")
	if got := alice.readLine(); got != "bob: hi alicia" {
		t.Fatalf("renamed client should still receive broadcasts, got %q", got)
	}
}

func TestWhisperCommandIsPrivate(t *testing.T) {
	addr := startTestServer(t)
	alice := dial(t, addr)
	bob := dial(t, addr)
	carol := dial(t, addr)
	alice.joinAs("alice")
	bob.joinAs("bob")
	alice.readLine() // bob join
	carol.joinAs("carol")
	alice.readLine() // carol join
	bob.readLine()   // carol join

	alice.send("/msg bob secret plans")
	if got := alice.readLine(); got != "[you -> bob]: secret plans" {
		t.Fatalf("alice's echo = %q", got)
	}
	if got := bob.readLine(); got != "[alice -> you]: secret plans" {
		t.Fatalf("bob received %q", got)
	}

	// carol should have nothing waiting -- prove it without blocking
	// forever by racing a short read against a broadcast that must land
	// first if it were ever going to arrive.
	alice.send("public message")
	if got := carol.readLine(); got != "alice: public message" {
		t.Fatalf("carol's next line = %q, want the public broadcast (no whisper leaked)", got)
	}
}

func TestQuitClosesTheConnection(t *testing.T) {
	addr := startTestServer(t)
	alice := dial(t, addr)
	alice.joinAs("alice")

	alice.send("/quit")
	alice.expectEOF()
}

func TestUnknownCommandGetsAnError(t *testing.T) {
	addr := startTestServer(t)
	alice := dial(t, addr)
	alice.joinAs("alice")

	alice.send("/frobnicate")
	if got := alice.readLine(); !strings.Contains(got, "unknown command") {
		t.Fatalf("unknown command reply = %q", got)
	}
}

func TestDisconnectWithoutQuitAnnouncesLeave(t *testing.T) {
	addr := startTestServer(t)
	alice := dial(t, addr)
	bob := dial(t, addr)
	alice.joinAs("alice")
	bob.joinAs("bob")
	alice.readLine() // bob join

	bob.conn.Close() // simulate a dropped connection, no /quit sent

	if got := alice.readLine(); got != "* bob has left (1 online)" {
		t.Fatalf("alice should be told bob left, got %q", got)
	}
}
