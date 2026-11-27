# Local proxy test TLS material

The CA certificate, server certificate and unencrypted server key here are intentionally public, generated test-only localhost material, not production credentials. Used only by NetworkTransportTest’s private QSslServer and a scoped CA configuration inside that test process; the CA private key was discarded. Normal application certificate verification stays enabled. The CA has certificate-signing usage; the separate leaf has server-auth usage and permits localhost and 127.0.0.1. The leaf expires in October 2027; regenerate before then. The test also rejects the same leaf without the scoped CA.

`silent-video-test.mp4` is an 0.8s, 64x64, 10fps MPEG-4 Visual test pattern generated locally with FFmpeg (lavfi testsrc2, no audio, faststart). It is synthetic decoder/HTTP Range evidence, not an upstream API sample or a product visual.

`plugin-websocket-test-{ca,cert,key}.pem` are separate generated test-only TLS material for the remote name `plugin-proxy.invalid` in PluginRuntimeTest. The private local proxy tunnels that otherwise unresolvable name to a loopback WebSocket server, covering wss proxy routing without Qt socket loopback bypass. Only the test process trusts this CA; its signing key was discarded. The leaf permits server authentication and expires in November 2027.
