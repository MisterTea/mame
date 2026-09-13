// MAMEHub Online launcher: static file server + archive.org candy proxy.
// Cross-compile for Windows: GOOS=windows GOARCH=amd64 go build -o MAMEHubOnline.exe .
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"
)

func main() {
	port := flag.Int("port", 8765, "HTTP listen port")
	bind := flag.String("bind", "127.0.0.1", "HTTP bind address")
	rootFlag := flag.String("root", "", "directory to serve (default: folder containing this executable)")
	cacheFlag := flag.String("cache", "", "candy download cache (default: <root>/.candy-cache)")
	openBrowser := flag.Bool("open", true, "open the default browser")
	flag.Parse()

	root, err := resolveRoot(*rootFlag)
	if err != nil {
		log.Fatal(err)
	}
	cache := *cacheFlag
	if cache == "" {
		cache = filepath.Join(root, ".candy-cache")
	}
	if err := os.MkdirAll(cache, 0o755); err != nil {
		log.Fatal(err)
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/candy-proxy", candyProxy(cache))
	mux.Handle("/", http.FileServer(http.Dir(root)))

	addr := fmt.Sprintf("%s:%d", *bind, *port)
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("listen %s: %v", addr, err)
	}

	urlStr := fmt.Sprintf("http://%s/", addr)
	fmt.Printf("MAMEHub Online (SNES)\nServing %s\n%s\nCandy proxy: /candy-proxy  cache: %s\nClose this window to stop the server.\n",
		root, urlStr, cache)

	if *openBrowser {
		go func() {
			time.Sleep(400 * time.Millisecond)
			if err := openURL(urlStr); err != nil {
				fmt.Fprintf(os.Stderr, "could not open browser: %v\nopen %s manually\n", err, urlStr)
			}
		}()
	}

	log.Fatal(http.Serve(ln, withCORS(mux)))
}

func resolveRoot(override string) (string, error) {
	if override != "" {
		return filepath.Abs(override)
	}
	exe, err := os.Executable()
	if err != nil {
		return "", err
	}
	exe, err = filepath.EvalSymlinks(exe)
	if err != nil {
		return "", err
	}
	dir := filepath.Dir(exe)
	// Dev convenience: running from launcher/ uses parent web/mamehub.
	if _, err := os.Stat(filepath.Join(dir, "index.html")); err != nil {
		parent := filepath.Clean(filepath.Join(dir, ".."))
		if _, err2 := os.Stat(filepath.Join(parent, "index.html")); err2 == nil {
			return parent, nil
		}
	}
	return dir, nil
}

func hostAllowed(host string) bool {
	host = strings.ToLower(strings.TrimSuffix(host, "."))
	return host == "archive.org" || strings.HasSuffix(host, ".archive.org")
}

func candyProxy(cacheDir string) http.HandlerFunc {
	client := &http.Client{Timeout: 180 * time.Second}
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		raw := r.URL.Query().Get("url")
		if raw == "" {
			http.Error(w, "missing url", http.StatusBadRequest)
			return
		}
		u, err := url.Parse(raw)
		if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" || !hostAllowed(u.Hostname()) {
			http.Error(w, "host not allowed", http.StatusForbidden)
			return
		}

		sum := sha256.Sum256([]byte(raw))
		key := hex.EncodeToString(sum[:])
		cachePath := filepath.Join(cacheDir, key+".bin")
		metaPath := filepath.Join(cacheDir, key+".url")

		w.Header().Set("Access-Control-Allow-Origin", "*")
		w.Header().Set("Cache-Control", "public, max-age=86400")

		if st, err := os.Stat(cachePath); err == nil && st.Size() > 0 {
			w.Header().Set("Content-Type", "application/octet-stream")
			w.Header().Set("X-Candy-Cache", "HIT")
			http.ServeFile(w, r, cachePath)
			return
		}

		req, err := http.NewRequest(http.MethodGet, raw, nil)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadGateway)
			return
		}
		req.Header.Set("User-Agent", "Mozilla/5.0 (MAMEHubBrowser; CandyProxy)")
		req.Header.Set("Accept", "*/*")
		resp, err := client.Do(req)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadGateway)
			return
		}
		defer resp.Body.Close()
		if resp.StatusCode >= 400 {
			http.Error(w, fmt.Sprintf("upstream HTTP %d", resp.StatusCode), resp.StatusCode)
			return
		}
		data, err := io.ReadAll(resp.Body)
		if err != nil {
			http.Error(w, err.Error(), http.StatusBadGateway)
			return
		}
		_ = os.WriteFile(cachePath, data, 0o644)
		_ = os.WriteFile(metaPath, []byte(raw+"\n"), 0o644)

		ct := resp.Header.Get("Content-Type")
		if ct == "" {
			ct = "application/octet-stream"
		}
		w.Header().Set("Content-Type", ct)
		w.Header().Set("Content-Length", fmt.Sprintf("%d", len(data)))
		w.Header().Set("X-Candy-Cache", "MISS")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(data)
	}
}

func withCORS(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Access-Control-Allow-Origin", "*")
		next.ServeHTTP(w, r)
	})
}

func openURL(u string) error {
	switch runtime.GOOS {
	case "windows":
		return exec.Command("rundll32", "url.dll,FileProtocolHandler", u).Start()
	case "darwin":
		return exec.Command("open", u).Start()
	default:
		return exec.Command("xdg-open", u).Start()
	}
}
