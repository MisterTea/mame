#!/usr/bin/env python3
"""Static file server + archive.org candy proxy for MAMEHub browser."""

from __future__ import annotations

import argparse
import hashlib
import http
import http.server
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request
from functools import partial
from pathlib import Path


def _host_allowed(host: str | None) -> bool:
	if not host:
		return False
	host = host.lower().rstrip(".")
	return host == "archive.org" or host.endswith(".archive.org")


class CandyHandler(http.server.SimpleHTTPRequestHandler):
	protocol_version = "HTTP/1.1"
	cache_dir: Path = Path(".")

	def do_GET(self):  # noqa: N802
		parsed = urllib.parse.urlparse(self.path)
		if parsed.path.rstrip("/") == "/candy-proxy":
			self._proxy_candy(parsed)
			return
		super().do_GET()

	def _proxy_candy(self, parsed: urllib.parse.ParseResult) -> None:
		qs = urllib.parse.parse_qs(parsed.query)
		urls = qs.get("url", [])
		if not urls:
			self.send_error(http.HTTPStatus.BAD_REQUEST, "missing url")
			return

		url = urls[0]
		try:
			parts = urllib.parse.urlparse(url)
		except Exception:
			self.send_error(http.HTTPStatus.BAD_REQUEST, "bad url")
			return

		if parts.scheme not in ("http", "https") or not parts.netloc or not _host_allowed(parts.hostname):
			self.send_error(http.HTTPStatus.FORBIDDEN, "host not allowed")
			return

		cache_key = hashlib.sha256(url.encode("utf-8")).hexdigest()
		cache_path = self.cache_dir / f"{cache_key}.bin"
		meta_path = self.cache_dir / f"{cache_key}.url"

		if cache_path.is_file() and cache_path.stat().st_size > 0:
			data = cache_path.read_bytes()
			self.send_response(http.HTTPStatus.OK)
			self.send_header("Content-Type", "application/octet-stream")
			self.send_header("Content-Length", str(len(data)))
			self.send_header("Cache-Control", "public, max-age=86400")
			self.send_header("Access-Control-Allow-Origin", "*")
			self.send_header("X-Candy-Cache", "HIT")
			self.end_headers()
			self.wfile.write(data)
			return

		headers = {
			"User-Agent": "Mozilla/5.0 (MAMEHubBrowser; CandyProxy)",
			"Accept": "*/*",
		}
		req = urllib.request.Request(url, headers=headers, method="GET")
		ctx = ssl.create_default_context()
		try:
			with urllib.request.urlopen(req, context=ctx, timeout=180) as resp:
				data = resp.read()
				content_type = resp.headers.get("Content-Type", "application/octet-stream")
				self.cache_dir.mkdir(parents=True, exist_ok=True)
				cache_path.write_bytes(data)
				meta_path.write_text(url + "\n", encoding="utf-8")
				self.send_response(http.HTTPStatus.OK)
				self.send_header("Content-Type", content_type)
				self.send_header("Content-Length", str(len(data)))
				self.send_header("Cache-Control", "public, max-age=86400")
				self.send_header("Access-Control-Allow-Origin", "*")
				self.send_header("X-Candy-Cache", "MISS")
				self.end_headers()
				self.wfile.write(data)
		except urllib.error.HTTPError as exc:
			self.send_error(exc.code, f"upstream HTTP {exc.code}")
		except Exception as exc:
			self.send_error(http.HTTPStatus.BAD_GATEWAY, str(exc))

	def log_message(self, fmt: str, *args) -> None:
		sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--port", type=int, default=8765)
	parser.add_argument("--bind", default="127.0.0.1")
	parser.add_argument(
		"--root",
		type=Path,
		default=Path(__file__).resolve().parent,
		help="directory to serve (default: this folder)",
	)
	parser.add_argument(
		"--cache",
		type=Path,
		default=Path(__file__).resolve().parent / ".candy-cache",
		help="directory for cached candy downloads",
	)
	args = parser.parse_args()
	root = args.root.resolve()
	cache = args.cache.resolve()
	cache.mkdir(parents=True, exist_ok=True)
	CandyHandler.cache_dir = cache
	handler = partial(CandyHandler, directory=str(root))
	server = http.server.ThreadingHTTPServer((args.bind, args.port), handler)
	print(
		f"Serving {root} on http://{args.bind}:{args.port}/ "
		f"(candy proxy at /candy-proxy, cache {cache})",
		flush=True,
	)
	try:
		server.serve_forever()
	except KeyboardInterrupt:
		print("\nStopped.", flush=True)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
