#!/usr/bin/env python3
# End-to-end test for chunked request body decoding in fox-http.
# Sends Transfer-Encoding: chunked POST /echo requests to a running
# hello_world, asserts the echoed body matches the reassembled chunks.

import argparse
import socket
import sys


def send(raw_request: bytes) -> bytes:
    s = socket.create_connection((ARGS.host, ARGS.port))
    s.sendall(raw_request)
    # Read status + headers + body. Simple approach: read until we see
    # \r\n\r\n then parse Content-Length and read that many body bytes.
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = s.recv(4096)
        if not chunk:
            break
        buf += chunk
    head, _, rest = buf.partition(b"\r\n\r\n")
    headers = head.decode(errors="replace")
    cl = 0
    for line in headers.split("\r\n"):
        if line.lower().startswith("content-length:"):
            cl = int(line.split(":", 1)[1].strip())
            break
    body = rest
    while len(body) < cl:
        chunk = s.recv(4096)
        if not chunk:
            break
        body += chunk
    s.close()
    return body


def chunk(data: bytes) -> bytes:
    return f"{len(data):x}\r\n".encode() + data + b"\r\n"


def build_request(chunks, extra_header=b""):
    body = b"".join(chunk(c) for c in chunks) + b"0\r\n\r\n"
    req = (
        b"POST /echo HTTP/1.1\r\n"
        b"Host: x\r\n"
        b"Transfer-Encoding: chunked\r\n"
        b"Content-Type: application/octet-stream\r\n"
        + extra_header
        + b"\r\n"
        + body
    )
    return req


def run_case(name, chunks, extra_header=b"", expected=None):
    if expected is None:
        expected = b"".join(chunks)
    raw = build_request(chunks, extra_header)
    got = send(raw)
    if got != expected:
        print(f"FAIL {name}:")
        print(f"  expected: {expected!r}")
        print(f"  got:      {got!r}")
        return False
    print(f"OK   {name}")
    return True


def main():
    results = []

    # 1. Single chunk
    results.append(run_case("single chunk", [b"hello world"]))

    # 2. Multiple small chunks
    results.append(run_case("multi chunks", [b"hel", b"lo ", b"wor", b"ld!"]))

    # 3. Chunks with extensions (should be ignored)
    def build_with_ext():
        body = (b"5;foo=bar\r\nhello\r\n"
                b"7;x=y;z\r\n world!\r\n"
                b"0\r\n\r\n")
        return (b"POST /echo HTTP/1.1\r\n"
                b"Host: x\r\n"
                b"Transfer-Encoding: chunked\r\n"
                b"\r\n" + body)
    got = send(build_with_ext())
    if got == b"hello world!":
        print("OK   chunk extensions ignored")
        results.append(True)
    else:
        print(f"FAIL chunk extensions: got {got!r}")
        results.append(False)

    # 4. Large body (multiple KB)
    big = bytes(range(256)) * 20  # 5120 bytes
    # Split into chunks of 300 bytes
    sliced = [big[i:i+300] for i in range(0, len(big), 300)]
    results.append(run_case("large body (5120B in 18 chunks)", sliced, expected=big))

    # 5. Trailer header (should be skipped, body still complete)
    def build_with_trailer():
        body = (b"5\r\nhello\r\n"
                b"0\r\n"
                b"X-Custom-Trailer: ignore-me\r\n"
                b"\r\n")
        return (b"POST /echo HTTP/1.1\r\n"
                b"Host: x\r\n"
                b"Transfer-Encoding: chunked\r\n"
                b"Trailer: X-Custom-Trailer\r\n"
                b"\r\n" + body)
    got = send(build_with_trailer())
    if got == b"hello":
        print("OK   trailers dropped")
        results.append(True)
    else:
        print(f"FAIL trailers: got {got!r}")
        results.append(False)

    # 6. Empty body (just 0-chunk)
    results.append(run_case("empty chunked body", [], expected=b""))

    passed = sum(results)
    total = len(results)
    print(f"\n{passed}/{total} passed")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    ARGS = parser.parse_args()
    main()
