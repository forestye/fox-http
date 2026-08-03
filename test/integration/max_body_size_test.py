#!/usr/bin/env python3
# End-to-end test for HttpServer::set_max_body_size. Expects hello_world to
# be running with a body limit of 1024 bytes. Over-limit requests (both
# Content-Length and chunked) must get "413 Payload Too Large" and a closed
# connection; under-limit requests must round-trip through POST /echo.

import argparse
import socket
import sys


def send(raw_request: bytes):
    """Returns (status_code, body) of the first response on a fresh connection."""
    s = socket.create_connection((ARGS.host, ARGS.port))
    try:
        s.sendall(raw_request)
    except BrokenPipeError:
        # Server may have replied 413 and shut down while we were still
        # sending the oversized body. Fall through and read the response.
        pass
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = s.recv(4096)
        if not chunk:
            break
        buf += chunk
    head, _, rest = buf.partition(b"\r\n\r\n")
    headers = head.decode(errors="replace")
    status = 0
    lines = headers.split("\r\n")
    if lines and lines[0].startswith("HTTP/1.1 "):
        status = int(lines[0].split(" ")[1])
    cl = 0
    for line in lines[1:]:
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
    return status, body


def fixed_request(body: bytes) -> bytes:
    return (b"POST /echo HTTP/1.1\r\n"
            b"Host: x\r\n"
            b"Content-Type: application/octet-stream\r\n"
            b"Content-Length: " + str(len(body)).encode() + b"\r\n"
            b"\r\n" + body)


def chunked_request(chunks) -> bytes:
    body = b"".join(f"{len(c):x}\r\n".encode() + c + b"\r\n" for c in chunks)
    body += b"0\r\n\r\n"
    return (b"POST /echo HTTP/1.1\r\n"
            b"Host: x\r\n"
            b"Transfer-Encoding: chunked\r\n"
            b"\r\n" + body)


def run_case(name, raw, want_status, want_body=None):
    status, body = send(raw)
    ok = status == want_status and (want_body is None or body == want_body)
    if ok:
        print(f"OK   {name}")
    else:
        print(f"FAIL {name}: status={status} body={body[:80]!r}")
    return ok


def main():
    limit = ARGS.limit
    results = [
        run_case("fixed body under limit",
                 fixed_request(b"a" * (limit - 1)),
                 200, b"a" * (limit - 1)),
        run_case("fixed body at limit",
                 fixed_request(b"a" * limit),
                 200, b"a" * limit),
        run_case("fixed body over limit",
                 fixed_request(b"a" * (limit + 1)),
                 413),
        run_case("fixed body far over limit (headers only)",
                 # Announce 100 MB but send nothing further: the server must
                 # reject on the announced Content-Length alone.
                 b"POST /echo HTTP/1.1\r\nHost: x\r\n"
                 b"Content-Length: 104857600\r\n\r\n",
                 413),
        run_case("chunked body under limit",
                 chunked_request([b"b" * 100, b"c" * 100]),
                 200, b"b" * 100 + b"c" * 100),
        run_case("chunked body over limit",
                 chunked_request([b"b" * 600, b"c" * 600]),
                 413),
    ]

    passed = sum(results)
    total = len(results)
    print(f"\n{passed}/{total} passed")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--limit", type=int, default=1024)
    ARGS = parser.parse_args()
    main()
