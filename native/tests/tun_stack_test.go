package tun

import (
	"bytes"
	"context"
	"io"
	"net"
	"sync"
	"testing"
	"time"

	xnet "github.com/xtls/xray-core/common/net"
	"github.com/xtls/xray-core/features/routing"
	"github.com/xtls/xray-core/transport"
	"gvisor.dev/gvisor/pkg/buffer"
	"gvisor.dev/gvisor/pkg/tcpip"
	"gvisor.dev/gvisor/pkg/tcpip/adapters/gonet"
	"gvisor.dev/gvisor/pkg/tcpip/link/channel"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv4"
	"gvisor.dev/gvisor/pkg/tcpip/network/ipv6"
	"gvisor.dev/gvisor/pkg/tcpip/stack"
)

// the channel pair exercises real TCP handshakes and UDP packets without host routes
type senkoChannelTun struct{ *channel.Endpoint }

func (t *senkoChannelTun) Start() error                             { return nil }
func (t *senkoChannelTun) Close() error                             { t.Endpoint.Close(); return nil }
func (t *senkoChannelTun) Name() (string, error)                    { return "senko-test", nil }
func (t *senkoChannelTun) Index() (int, error)                      { return 1, nil }
func (t *senkoChannelTun) newEndpoint() (stack.LinkEndpoint, error) { return t.Endpoint, nil }

type senkoEchoDispatcher struct{ destinations chan xnet.Destination }

func (d *senkoEchoDispatcher) Type() interface{} { return routing.DispatcherType() }
func (d *senkoEchoDispatcher) Start() error      { return nil }
func (d *senkoEchoDispatcher) Close() error      { return nil }
func (d *senkoEchoDispatcher) Dispatch(context.Context, xnet.Destination) (*transport.Link, error) {
	return nil, io.ErrUnexpectedEOF
}
func (d *senkoEchoDispatcher) DispatchLink(_ context.Context, dst xnet.Destination, link *transport.Link) error {
	d.destinations <- dst
	for {
		mb, err := link.Reader.ReadMultiBuffer()
		if len(mb) > 0 {
			if writeErr := link.Writer.WriteMultiBuffer(mb); writeErr != nil {
				return writeErr
			}
		}
		if err != nil {
			return err
		}
	}
}

func TestSenkoStackRoundTrip(t *testing.T) {
	for _, test := range []struct {
		name, address string
		port          uint16
		udp           bool
	}{
		{"http", "203.0.113.1", 80, false},
		{"tls-port-binary", "203.0.113.1", 443, false},
		{"messaging-port", "203.0.113.1", 5222, false},
		{"ipv6-binary", "2001:db8::1", 5222, false},
		{"dns", "203.0.113.1", 53, true},
		{"ipv6-udp", "2001:db8::1", 443, true},
	} {
		t.Run(test.name, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
			defer cancel()
			left, right := channel.New(64, 1500, ""), channel.New(64, 1500, "")
			client, err := createStack(left)
			if err != nil {
				t.Fatal(err)
			}
			defer client.Close()
			defer left.Close()
			defer right.Close()
			for _, address := range []string{"198.18.0.1", "fd00::1"} {
				ip := tcpip.AddrFromSlice(net.ParseIP(address).To16())
				protocol := ipv6.ProtocolNumber
				if v4 := net.ParseIP(address).To4(); v4 != nil {
					ip = tcpip.AddrFromSlice(v4)
					protocol = ipv4.ProtocolNumber
				}
				if err := client.AddProtocolAddress(defaultNIC, tcpip.ProtocolAddress{Protocol: protocol, AddressWithPrefix: ip.WithPrefix()}, stack.AddressProperties{}); err != nil {
					t.Fatal(err)
				}
			}
			dispatcher := &senkoEchoDispatcher{destinations: make(chan xnet.Destination, 1)}
			handler := &Handler{ctx: ctx, config: &Config{}, dispatcher: dispatcher}
			server, err := NewStack(ctx, StackOptions{Tun: &senkoChannelTun{right}, IdleTimeout: time.Second}, handler)
			if err != nil {
				t.Fatal(err)
			}
			if err := server.Start(); err != nil {
				t.Fatal(err)
			}
			defer server.Close()
			var pumps sync.WaitGroup
			for _, pair := range [][2]*channel.Endpoint{{left, right}, {right, left}} {
				pumps.Add(1)
				go func(from, to *channel.Endpoint) {
					defer pumps.Done()
					for {
						p := from.ReadContext(ctx)
						if p == nil {
							return
						}
						copy := stack.NewPacketBuffer(stack.PacketBufferOptions{Payload: buffer.MakeWithView(p.ToView())})
						to.InjectInbound(p.NetworkProtocolNumber, copy)
						copy.DecRef()
						p.DecRef()
					}
				}(pair[0], pair[1])
			}
			defer func() { cancel(); pumps.Wait() }()
			ip := net.ParseIP(test.address)
			protocol := ipv6.ProtocolNumber
			if v4 := ip.To4(); v4 != nil {
				ip = v4
				protocol = ipv4.ProtocolNumber
			} else {
				ip = ip.To16()
			}
			remote := tcpip.FullAddress{NIC: defaultNIC, Addr: tcpip.AddrFromSlice(ip), Port: test.port}
			var conn net.Conn
			if test.udp {
				conn, err = gonet.DialUDP(client, nil, &remote, protocol)
			} else {
				conn, err = gonet.DialContextTCP(ctx, client, remote, protocol)
			}
			if err != nil {
				t.Fatal(err)
			}
			defer conn.Close()
			conn.SetDeadline(time.Now().Add(2 * time.Second))
			payload := bytes.Repeat([]byte{0xef, 0x01, 0x00, 0x84, 0x52}, 100)
			if _, err := conn.Write(payload); err != nil {
				t.Fatal(err)
			}
			got := make([]byte, len(payload))
			if _, err := io.ReadFull(conn, got); err != nil {
				t.Fatal(err)
			}
			if !bytes.Equal(got, payload) {
				t.Fatal("binary stream changed")
			}
			select {
			case dst := <-dispatcher.destinations:
				if !dst.Address.IP().Equal(ip) || uint16(dst.Port) != test.port {
					t.Fatalf("wrong route: %v", dst)
				}
			case <-ctx.Done():
				t.Fatal("flow was not dispatched")
			}
		})
	}
}
