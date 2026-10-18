package tun

import (
	"bytes"
	stderrors "errors"
	"io"
	stdnet "net"
	"os"
	"testing"
	"time"

	"github.com/xtls/xray-core/common/buf"
	xnet "github.com/xtls/xray-core/common/net"
)

func senkoUDPFixture(t *testing.T, write func([]byte, xnet.Destination, xnet.Destination) error) (*udpConnectionHandler, *udpConn, xnet.Destination, xnet.Destination) {
	t.Helper()
	ready := make(chan *udpConn, 2)
	if write == nil {
		write = func([]byte, xnet.Destination, xnet.Destination) error { return nil }
	}
	u := newUdpConnectionHandler(func(c stdnet.Conn, _ xnet.Destination) { ready <- c.(*udpConn) }, write)
	src := xnet.UDPDestination(xnet.IPAddress([]byte{198, 18, 0, 1}), 53000)
	dst := xnet.UDPDestination(xnet.IPAddress([]byte{8, 8, 8, 8}), 53)
	u.HandlePacket(src, dst, []byte{0x12, 0x34})
	var c *udpConn
	select {
	case c = <-ready:
	case <-time.After(time.Second):
		t.Fatal("UDP flow did not start")
	}
	t.Cleanup(u.Close)
	got := make([]byte, 2)
	if n, err := c.Read(got); err != nil || n != 2 {
		t.Fatalf("first packet: %d %v", n, err)
	}
	return u, c, src, dst
}

func TestSenkoUDPDeadline(t *testing.T) {
	_, c, _, _ := senkoUDPFixture(t, nil)
	c.SetReadDeadline(time.Now().Add(-time.Second))
	done := make(chan error, 1)
	go func() { _, err := c.Read(make([]byte, 64)); done <- err }()
	select {
	case err := <-done:
		if !stderrors.Is(err, os.ErrDeadlineExceeded) {
			t.Fatalf("deadline error: %v", err)
		}
	case <-time.After(200 * time.Millisecond):
		c.Close()
		<-done
		t.Fatal("an expired UDP deadline left its reader blocked")
	}
}

func TestSenkoUDPCloseKeepsReplacement(t *testing.T) {
	u, old, src, dst := senkoUDPFixture(t, nil)
	old.Close()
	u.HandlePacket(src, dst, []byte{1})
	u.RLock()
	replacement := u.udpConns[src]
	u.RUnlock()
	if replacement == nil || replacement == old {
		t.Fatal("replacement flow missing")
	}
	defer replacement.Close()
	old.Close()
	u.RLock()
	current := u.udpConns[src]
	u.RUnlock()
	if current != replacement {
		t.Fatal("closing an old flow removed the new flow at the same source port")
	}
}

func TestSenkoUDPWriteFailure(t *testing.T) {
	want := stderrors.New("utun write failed")
	_, c, _, _ := senkoUDPFixture(t, func([]byte, xnet.Destination, xnet.Destination) error { return want })
	if n, err := c.Write([]byte{1}); n != 0 || !stderrors.Is(err, want) {
		t.Fatalf("write failure was hidden: n=%d err=%v", n, err)
	}
}

func TestSenkoUDPLargeDatagram(t *testing.T) {
	u, c, src, dst := senkoUDPFixture(t, nil)
	payload := bytes.Repeat([]byte{0x94, 0x21, 0x56}, 4096)
	u.HandlePacket(src, dst, payload)
	done := make(chan buf.MultiBuffer, 1)
	go func() { mb, _ := c.ReadMultiBuffer(); done <- mb }()
	select {
	case mb := <-done:
		defer buf.ReleaseMulti(mb)
		if len(mb) != 1 || !bytes.Equal(mb[0].Bytes(), payload) || mb[0].UDP == nil || *mb[0].UDP != dst {
			t.Fatal("datagram bytes or destination changed")
		}
	case <-time.After(200 * time.Millisecond):
		c.Close()
		mb := <-done
		buf.ReleaseMulti(mb)
		t.Fatal("a valid large UDP datagram was dropped")
	}
}

func TestSenkoUDPClosedRead(t *testing.T) {
	_, c, _, _ := senkoUDPFixture(t, nil)
	c.Close()
	if _, err := c.Read(make([]byte, 1)); !stderrors.Is(err, io.EOF) {
		t.Fatalf("closed read: %v", err)
	}
}

func TestSenkoUDPDeadlineChange(t *testing.T) {
	_, c, _, _ := senkoUDPFixture(t, nil)
	done := make(chan error, 1)
	go func() { _, err := c.Read(make([]byte, 1)); done <- err }()
	c.SetReadDeadline(time.Now().Add(10 * time.Millisecond))
	select {
	case err := <-done:
		if !stderrors.Is(err, os.ErrDeadlineExceeded) {
			t.Fatalf("changed deadline: %v", err)
		}
	case <-time.After(time.Second):
		t.Fatal("new deadline did not wake an existing read")
	}
	c.SetReadDeadline(time.Time{})
	c.handler.HandlePacket(c.src, c.dst, []byte{7})
	if n, err := c.Read(make([]byte, 1)); n != 1 || err != nil {
		t.Fatalf("reset deadline: %d %v", n, err)
	}
	c.SetWriteDeadline(time.Now().Add(-time.Second))
	if _, err := c.Write([]byte{1}); !stderrors.Is(err, os.ErrDeadlineExceeded) {
		t.Fatalf("write deadline: %v", err)
	}
}

func TestSenkoUDPIdleCleanup(t *testing.T) {
	u, c, src, _ := senkoUDPFixture(t, nil)
	c.deadlineMu.Lock()
	c.lastActivity = time.Now().Add(-time.Hour)
	c.deadlineMu.Unlock()
	if _, err := c.Read(make([]byte, 1)); !stderrors.Is(err, os.ErrDeadlineExceeded) {
		t.Fatalf("idle read: %v", err)
	}
	u.RLock()
	current := u.udpConns[src]
	u.RUnlock()
	if current != nil {
		t.Fatal("idle flow retained")
	}
}

func TestSenkoUDPQueueOwnershipAndBounds(t *testing.T) {
	u, c, src, dst := senkoUDPFixture(t, nil)
	payload := []byte{1, 2, 3}
	u.HandlePacket(src, dst, payload)
	payload[0] = 9
	p := make([]byte, 3)
	if _, err := c.Read(p); err != nil || p[0] != 1 {
		t.Fatalf("packet storage was borrowed: %v %v", p, err)
	}
	large := make([]byte, 60000)
	for i := 0; i < 100; i++ {
		u.HandlePacket(src, dst, large)
	}
	u.RLock()
	queued := u.queuedBytes
	u.RUnlock()
	if queued <= 0 || queued > udpFlowBytesLimit {
		t.Fatalf("per-flow queue: %d", queued)
	}
	u.Close()
	u.RLock()
	queued, flows := u.queuedBytes, len(u.udpConns)
	u.RUnlock()
	if queued != 0 || flows != 0 {
		t.Fatalf("close retained %d bytes, %d flows", queued, flows)
	}
	u.HandlePacket(src, dst, []byte{1})
	if len(u.udpConns) != 0 {
		t.Fatal("closed handler accepted a flow")
	}
}

func TestSenkoUDPMultipleDestinations(t *testing.T) {
	var reply xnet.Destination
	u, c, src, dst := senkoUDPFixture(t, func(_ []byte, from, _ xnet.Destination) error { reply = from; return nil })
	other := xnet.UDPDestination(xnet.IPAddress([]byte{1, 1, 1, 1}), 443)
	u.HandlePacket(src, other, []byte{8})
	mb, err := c.ReadMultiBuffer()
	if err != nil {
		t.Fatal(err)
	}
	defer buf.ReleaseMulti(mb)
	if len(mb) != 1 || mb[0].UDP == nil || *mb[0].UDP != other {
		t.Fatal("full-cone destination lost")
	}
	b := buf.New()
	b.Write([]byte{5})
	b.UDP = &dst
	if err := c.WriteMultiBuffer(buf.MultiBuffer{b}); err != nil || reply != dst {
		t.Fatalf("reply destination: %v %v", reply, err)
	}
}

func TestSenkoUDPGlobalBounds(t *testing.T) {
	u := newUdpConnectionHandler(func(stdnet.Conn, xnet.Destination) {},
		func([]byte, xnet.Destination, xnet.Destination) error { return nil })
	defer u.Close()
	dst := xnet.UDPDestination(xnet.IPAddress([]byte{203, 0, 113, 1}), 443)
	payload := make([]byte, 60000)
	for i := 0; i < udpFlowLimit+10; i++ {
		src := xnet.UDPDestination(xnet.IPAddress([]byte{198, 18, 0, 1}), xnet.Port(10000+i))
		for j := 0; j < 3; j++ {
			u.HandlePacket(src, dst, payload)
		}
	}
	if len(u.udpConns) != udpFlowLimit || u.queuedBytes > udpQueueBytesLimit {
		t.Fatalf("global bounds: %d flows, %d bytes", len(u.udpConns), u.queuedBytes)
	}
	u.Close()
	if len(u.udpConns) != 0 || u.queuedBytes != 0 {
		t.Fatal("queued packets survived shutdown")
	}
}

func TestSenkoUDPDatagramLimit(t *testing.T) {
	u, c, src, dst := senkoUDPFixture(t, nil)
	u.HandlePacket(src, dst, make([]byte, 65508))
	if len(c.egress) != 0 {
		t.Fatal("oversized IPv4 UDP packet accepted")
	}
	if _, err := c.Write(make([]byte, 65508)); err == nil {
		t.Fatal("oversized IPv4 reply accepted")
	}
	c.SetReadDeadline(time.Now().Add(time.Second))
	u.HandlePacket(src, dst, nil)
	if n, err := c.Read(make([]byte, 1)); n != 0 || err != nil {
		t.Fatalf("empty datagram: %d %v", n, err)
	}
}
