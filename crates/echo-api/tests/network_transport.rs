//! A real loopback HTTP proxy verifies the production API builder, without any
//! upstream DTO fixture, account credential or real remote service.
use echo_api::util::request::{ApiClient, Endpoint, Identity, Origin};
use std::{
    collections::BTreeMap,
    io::{Read, Write},
    net::{TcpListener, TcpStream},
    time::{Duration, Instant},
};

fn accept_before(listener: &TcpListener, deadline: Instant) -> TcpStream {
    loop {
        match listener.accept() {
            Ok((peer, _)) => return peer,
            Err(error)
                if error.kind() == std::io::ErrorKind::WouldBlock && Instant::now() < deadline =>
            {
                std::thread::sleep(Duration::from_millis(2));
            }
            other => panic!("fault server did not receive a connection: {other:?}"),
        }
    }
}

// Read the complete fixed-length request before injecting a failure. Merely
// accepting a socket would not establish whether a write might have committed.
fn read_complete_request(peer: &mut TcpStream) -> Vec<u8> {
    peer.set_nonblocking(false).unwrap();
    peer.set_read_timeout(Some(Duration::from_secs(2))).unwrap();
    let mut bytes = Vec::new();
    let mut buffer = [0; 2048];
    loop {
        let count = peer.read(&mut buffer).unwrap();
        assert!(count > 0, "request ended before its body was received");
        bytes.extend_from_slice(&buffer[..count]);
        assert!(bytes.len() < 32 * 1024);
        if let Some(end) = bytes.windows(4).position(|v| v == b"\r\n\r\n") {
            let header = std::str::from_utf8(&bytes[..end]).unwrap();
            let length = header
                .lines()
                .find_map(|line| {
                    let (name, value) = line.split_once(':')?;
                    name.eq_ignore_ascii_case("content-length")
                        .then(|| value.trim().parse::<usize>().unwrap())
                })
                .unwrap_or(0);
            if bytes.len() >= end + 4 + length {
                return bytes;
            }
        }
    }
}

#[tokio::test]
async fn api_uses_the_host_transport_for_new_requests() {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    listener.set_nonblocking(true).unwrap();
    let server = std::thread::spawn(move || {
        let started = Instant::now();
        let mut stream = loop {
            match listener.accept() {
                Ok((stream, _)) => break stream,
                Err(error)
                    if error.kind() == std::io::ErrorKind::WouldBlock
                        && started.elapsed() < Duration::from_secs(5) =>
                {
                    std::thread::sleep(Duration::from_millis(5));
                }
                other => panic!("proxy did not receive the API request: {other:?}"),
            }
        };
        stream.set_nonblocking(false).unwrap();
        stream
            .set_read_timeout(Some(Duration::from_secs(2)))
            .unwrap();
        let mut request = Vec::new();
        let mut buffer = [0; 2048];
        while !request.windows(4).any(|v| v == b"\r\n\r\n") {
            let count = stream.read(&mut buffer).unwrap();
            assert!(count > 0);
            request.extend_from_slice(&buffer[..count]);
        }
        stream
            .write_all(
                b"HTTP/1.1 200 OK\r\nContent-Length: 12\r\nConnection: close\r\n\r\ntransport-ok",
            )
            .unwrap();
        String::from_utf8(request).unwrap()
    });
    let client = ApiClient::new().unwrap();
    assert!(client.set_transport_port(port));
    assert!(!client.set_transport_port(0));
    assert!(client.set_timeout_seconds(2.));
    let identity = Identity {
        userid: 0,
        token: String::new(),
        t1: String::new(),
        mid: "transport-test".into(),
        dfid: "transport-test".into(),
        uuid: "-".into(),
        guid: String::new(),
        mac: String::new(),
        device_name: String::new(),
        render_fingerprint: String::new(),
    };
    let response = client
        .request_effect_catalog(
            &identity,
            Endpoint {
                origin: Origin::EffectsCatalog,
                path: "/transport-test",
                router: None,
                method: reqwest::Method::GET,
                params: BTreeMap::new(),
                body: Vec::new(),
                headers: &[],
            },
        )
        .await
        .unwrap();
    assert_eq!(response, b"transport-ok");
    let request = server.join().unwrap();
    assert!(request.starts_with("GET http://mobilecdngz.kugou.com/transport-test?"));
    assert!(
        !request
            .to_ascii_lowercase()
            .contains("proxy-authorization:")
    );
}

#[tokio::test]
async fn mcloud_transport_connects_only_to_the_cloud_host_and_keeps_secrets_encrypted() {
    // https origins tunnel through the loopback proxy with CONNECT, so the
    // observable facts are the exact tunnel target and the TLS handshake that
    // follows; the query-shape security property is asserted directly by
    // user_cloud::transport_params' unit test.
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    listener.set_nonblocking(true).unwrap();
    let server = std::thread::spawn(move || {
        let started = Instant::now();
        let mut stream = loop {
            match listener.accept() {
                Ok((stream, _)) => break stream,
                Err(error)
                    if error.kind() == std::io::ErrorKind::WouldBlock
                        && started.elapsed() < Duration::from_secs(5) =>
                {
                    std::thread::sleep(Duration::from_millis(5));
                }
                other => panic!("proxy did not receive the cloud request: {other:?}"),
            }
        };
        stream.set_nonblocking(false).unwrap();
        stream
            .set_read_timeout(Some(Duration::from_secs(2)))
            .unwrap();
        let mut head = Vec::new();
        let mut buffer = [0; 2048];
        while !head.windows(4).any(|v| v == b"\r\n\r\n") {
            let count = stream.read(&mut buffer).unwrap();
            assert!(count > 0);
            head.extend_from_slice(&buffer[..count]);
        }
        stream
            .write_all(b"HTTP/1.1 200 Connection established\r\n\r\n")
            .unwrap();
        // The client must now start a TLS handshake (record type 0x16).
        // BSD accepts inherit the listener's nonblocking flag, so the TLS
        // record read retries on WouldBlock with a bounded deadline.
        let mut tls = [0u8; 5];
        let mut read = 0;
        let deadline = Instant::now() + Duration::from_secs(2);
        while read < tls.len() && Instant::now() < deadline {
            match stream.read(&mut tls[read..]) {
                Ok(0) => break,
                Ok(count) => read += count,
                Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {
                    std::thread::sleep(Duration::from_millis(5));
                }
                Err(error) => panic!("TLS read failed: {error:?}"),
            }
        }
        assert_eq!(read, tls.len(), "TLS handshake missing");
        (String::from_utf8(head).unwrap(), tls)
    });
    let client = ApiClient::new().unwrap();
    assert!(client.set_transport_port(port));
    assert!(client.set_timeout_seconds(2.));
    let identity: Identity = serde_json::from_value(serde_json::json!({
        "userid": 123,
        "token": "fixture-secret",
        "t1": "fixture-refresh",
        "mid": "fixture-mid",
        "dfid": "fixture-dfid"
    }))
    .unwrap();
    let result = echo_api::module::user_cloud::execute(
        &client,
        &identity,
        echo_api::module::user_cloud::Query {
            page: 1,
            page_size: 30,
        },
    )
    .await;
    // The loopback proxy cannot complete a real TLS handshake; the request
    // must fail as a transport error, never be downgraded or retried.
    assert!(result.is_err());
    let (head, tls) = server.join().unwrap();
    assert!(
        head.starts_with("CONNECT mcloudservice.kugou.com:443 HTTP/1.1"),
        "unexpected tunnel head: {}",
        head.lines().next().unwrap_or_default()
    );
    let lower = head.to_ascii_lowercase();
    assert!(lower.contains("host: mcloudservice.kugou.com:443"));
    for excluded in [
        "fixture-secret",
        "fixture-refresh",
        "fixture-mid",
        "fixture-dfid",
    ] {
        assert!(!lower.contains(excluded), "tunnel leaked {excluded}");
    }
    assert_eq!(tls[0], 0x16, "expected a TLS handshake record");
    assert_eq!(tls[1], 0x03, "expected a TLS handshake version");
}

/// Fault injection exercises HTTP framing and deadlines, without inventing any
/// provider response DTO. Secrets in URLs/bodies must not enter error payloads.
#[tokio::test]
async fn failures_preserve_stage_status_and_safe_counters() {
    for (reply, pause, phase, status, kind) in [
        (&b"HTTP/1.1 503 Unavailable\r\nContent-Length: 15\r\nConnection: close\r\n\r\nresponse-secret"[..], false, "response_status", Some(503), "http"),
        (&b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\nab"[..], false, "response_body", Some(200), "transport"),
        (&b""[..], true, "send", None, "transport"),
        (&b"HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\nab"[..], true, "response_body", Some(200), "transport"),
    ] {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        listener.set_nonblocking(true).unwrap();
        let server = std::thread::spawn(move || {
            let deadline = Instant::now() + Duration::from_secs(5);
            let mut stream = loop {
                match listener.accept() {
                    Ok((stream, _)) => break stream,
                    Err(error) if error.kind() == std::io::ErrorKind::WouldBlock && Instant::now() < deadline => std::thread::sleep(Duration::from_millis(5)),
                    other => panic!("fault server did not receive a request: {other:?}"),
                }
            };
            stream.set_nonblocking(false).unwrap();
            stream.set_read_timeout(Some(Duration::from_secs(2))).unwrap();
            let mut head = Vec::new();
            let mut buffer = [0; 2048];
            while !head.windows(4).any(|v| v == b"\r\n\r\n") {
                let count = stream.read(&mut buffer).unwrap();
                assert!(count > 0);
                head.extend_from_slice(&buffer[..count]);
            }
            stream.write_all(reply).unwrap();
            // Give the async consumer time to receive the partial body before
            // EOF, or deliberately hold the connection past its deadline.
            std::thread::sleep(Duration::from_millis(if pause { 500 } else { 20 }));
        });
        let client = ApiClient::new().unwrap();
        assert!(client.set_transport_port(port));
        assert!(client.set_timeout_seconds(if pause { 0.2 } else { 2. }));
        let identity: Identity = serde_json::from_value(serde_json::json!({
            "userid": 123, "mid": "device-secret", "dfid": "fingerprint-secret", "token": "credential-secret"
        })).unwrap();
        let error = client.request_effect_catalog(&identity, Endpoint {
            origin: Origin::EffectsCatalog,
            path: "/diagnostic-test",
            router: None,
            method: reqwest::Method::GET,
            params: BTreeMap::from([("private".into(), "query-secret".into())]),
            body: Vec::new(), headers: &[],
        }).await.unwrap_err();
        server.join().unwrap();
        assert_eq!(error.kind, kind);
        let diagnostic = error.diagnostics.as_ref().unwrap();
        assert_eq!(diagnostic.phase, phase);
        assert_eq!(diagnostic.http_status, status);
        assert!(diagnostic.elapsed_ms < 2000);
        assert_eq!(diagnostic.received_bytes, if phase == "response_status" { 15 } else if phase == "send" { 0 } else { 2 });
        if pause {
            assert_eq!(error.transport, Some("timeout"));
            assert!(diagnostic.elapsed_ms >= 150);
        }
        let serialized = serde_json::to_string(&error).unwrap();
        for secret in ["query-secret", "credential-secret", "device-secret", "fingerprint-secret", "response-secret", "mobilecdngz.kugou.com", "/diagnostic-test"] {
            assert!(!serialized.contains(secret));
        }
    }
}

#[tokio::test]
async fn refused_connection_has_a_typed_cause_without_an_address() {
    // Reserve an ephemeral port, then close its listener to exercise refusal.
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    let client = ApiClient::new().unwrap();
    assert!(client.set_transport_port(port));
    assert!(client.set_timeout_seconds(0.1));
    drop(listener);
    let identity: Identity =
        serde_json::from_value(serde_json::json!({"mid":"test", "dfid":"test"})).unwrap();
    let error = client
        .request_effect_catalog(
            &identity,
            Endpoint {
                origin: Origin::EffectsCatalog,
                path: "/test",
                router: None,
                method: reqwest::Method::GET,
                params: BTreeMap::new(),
                body: Vec::new(),
                headers: &[],
            },
        )
        .await
        .unwrap_err();
    assert_eq!(error.transport, Some("connect"));
    let diagnostic = error.diagnostics.as_ref().unwrap();
    assert_eq!(diagnostic.phase, "send");
    assert_eq!(diagnostic.io_kind, Some("connection_refused"));
    assert_eq!(diagnostic.http_status, None);
    assert!(!serde_json::to_string(&error).unwrap().contains("127.0.0.1"));
}

#[tokio::test]
async fn sequential_posts_reuse_connections_until_the_idle_deadline() {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    listener.set_nonblocking(true).unwrap();
    let server = std::thread::spawn(move || {
        let deadline = Instant::now() + Duration::from_secs(16);
        let mut peers = Vec::new();
        let mut buffers: Vec<Vec<u8>> = Vec::new();
        let mut observed = Vec::new();
        while observed.len() < 4 && Instant::now() < deadline {
            if let Ok((peer, _)) = listener.accept() {
                peer.set_nonblocking(true).unwrap();
                peers.push(peer);
                buffers.push(Vec::new());
            }
            for (index, peer) in peers.iter_mut().enumerate() {
                let mut bytes = [0; 4096];
                match peer.read(&mut bytes) {
                    Ok(0) => continue,
                    Ok(count) => buffers[index].extend_from_slice(&bytes[..count]),
                    Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => continue,
                    Err(error) => panic!("keepalive peer failed: {error:?}"),
                }
                if buffers[index].windows(4).any(|v| v == b"\r\n\r\n") {
                    assert!(buffers[index].starts_with(b"POST "));
                    observed.push(index);
                    buffers[index].clear();
                    // Deliberately advertise keepalive and keep the peer open.
                    // Short bursts should reuse this socket. Once the local
                    // idle deadline passes, a new connection is required.
                    peer.write_all(
                        b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok",
                    )
                    .unwrap();
                }
            }
            std::thread::sleep(Duration::from_millis(2));
        }
        observed
    });
    let client = ApiClient::new().unwrap();
    assert!(client.set_transport_port(port));
    assert!(client.set_timeout_seconds(2.));
    let identity: Identity =
        serde_json::from_value(serde_json::json!({"mid":"test", "dfid":"test"})).unwrap();
    for index in 0..4 {
        if index == 3 {
            // Exercise the production ten-second policy, without adding a
            // test-only connection setting or another runtime dependency.
            tokio::time::sleep(Duration::from_secs(11)).await;
        }
        assert_eq!(
            client
                .request_effect_catalog(
                    &identity,
                    Endpoint {
                        origin: Origin::EffectsCatalog,
                        path: "/keepalive-test",
                        router: None,
                        method: reqwest::Method::POST,
                        params: BTreeMap::new(),
                        body: Vec::new(),
                        headers: &[],
                    }
                )
                .await
                .unwrap(),
            b"ok"
        );
    }
    assert_eq!(server.join().unwrap(), vec![0, 0, 0, 1]);
}

#[tokio::test]
async fn peer_closed_idle_connection_is_replaced_for_the_next_post() {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    listener.set_nonblocking(true).unwrap();
    let server = std::thread::spawn(move || {
        let deadline = Instant::now() + Duration::from_secs(5);
        for _ in 0..2 {
            let mut peer = accept_before(&listener, deadline);
            let bytes = read_complete_request(&mut peer);
            assert!(bytes.starts_with(b"POST "));
            peer.write_all(
                b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok",
            )
            .unwrap();
            // The peer advertises keepalive but then closes while idle.
            // The next request must still succeed using a new socket.
        }
    });
    let client = ApiClient::new().unwrap();
    assert!(client.set_transport_port(port));
    assert!(client.set_timeout_seconds(2.));
    let identity: Identity =
        serde_json::from_value(serde_json::json!({"mid":"test", "dfid":"test"})).unwrap();
    for _ in 0..2 {
        let bytes = client
            .request_effect_catalog(
                &identity,
                Endpoint {
                    origin: Origin::EffectsCatalog,
                    path: "/closed-idle-test",
                    router: None,
                    method: reqwest::Method::POST,
                    params: BTreeMap::new(),
                    body: b"probe".to_vec(),
                    headers: &[],
                },
            )
            .await
            .unwrap();
        assert_eq!(bytes, b"ok");
        tokio::time::sleep(Duration::from_millis(30)).await;
    }
    server.join().unwrap();
}

#[tokio::test]
async fn post_with_a_received_body_and_lost_response_is_not_replayed() {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    listener.set_nonblocking(true).unwrap();
    let server = std::thread::spawn(move || {
        let mut peer = accept_before(&listener, Instant::now() + Duration::from_secs(5));
        let warmup = read_complete_request(&mut peer);
        assert!(warmup.ends_with(b"warmup"));
        peer.write_all(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: keep-alive\r\n\r\nok")
            .unwrap();
        // The uncertain write uses a reused connection, the same situation in
        // which unstarted requests may safely be recovered by the HTTP client.
        let request = read_complete_request(&mut peer);
        assert!(request.starts_with(b"POST "));
        assert!(request.ends_with(b"mutation-probe"));
        // A server could have committed here. Drop the response entirely.
        drop(peer);
        let deadline = Instant::now() + Duration::from_millis(300);
        let mut count = 2;
        while Instant::now() < deadline {
            match listener.accept() {
                Ok((peer, _)) => {
                    count += 1;
                    drop(peer);
                }
                Err(error) if error.kind() == std::io::ErrorKind::WouldBlock => {
                    std::thread::sleep(Duration::from_millis(2));
                }
                Err(error) => panic!("retry observation failed: {error:?}"),
            }
        }
        count
    });
    let client = ApiClient::new().unwrap();
    assert!(client.set_transport_port(port));
    assert!(client.set_timeout_seconds(2.));
    let identity: Identity =
        serde_json::from_value(serde_json::json!({"mid":"test", "dfid":"test"})).unwrap();
    let warmup = client
        .request_effect_catalog(
            &identity,
            Endpoint {
                origin: Origin::EffectsCatalog,
                path: "/uncertain-write-test",
                router: None,
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body: b"warmup".to_vec(),
                headers: &[],
            },
        )
        .await
        .unwrap();
    assert_eq!(warmup, b"ok");
    let error = client
        .request_effect_catalog(
            &identity,
            Endpoint {
                origin: Origin::EffectsCatalog,
                path: "/uncertain-write-test",
                router: None,
                method: reqwest::Method::POST,
                params: BTreeMap::new(),
                body: b"mutation-probe".to_vec(),
                headers: &[],
            },
        )
        .await
        .unwrap_err();
    assert_eq!(error.kind, "transport");
    assert_eq!(server.join().unwrap(), 2);
}
