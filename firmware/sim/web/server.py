# Web simulator for the clock: python3 -I server.py <port> <path-to-simd>
# Serves index.html and draws frames with simd (same scene.h as the clock) at /frame.
import http.server, socketserver, subprocess, sys, os, threading, urllib.parse
port, simd = int(sys.argv[1]), sys.argv[2]
here = os.path.dirname(os.path.abspath(__file__))
proc = subprocess.Popen([simd], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
lock = threading.Lock()
def frame(q):
    g = lambda k, d: int(q.get(k, [d])[0])
    line = '%d %d %d %d %d %d %d\n' % (g('m', 1), g('t', 0), g('s', 0), g('w', 0), g('d', 0), g('a', 0), g('o', 1))
    with lock:
        proc.stdin.write(line.encode()); proc.stdin.flush()
        n = int.from_bytes(proc.stdout.read(4), 'little')
        return proc.stdout.read(n)
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == '/frame':
            body, ct = frame(urllib.parse.parse_qs(u.query)), 'image/bmp'
        elif u.path in ('/', '/index.html'):
            body, ct = open(os.path.join(here, 'index.html'), 'rb').read(), 'text/html; charset=utf-8'
        else:
            self.send_error(404); return
        self.send_response(200); self.send_header('Content-Type', ct)
        self.send_header('Cache-Control', 'no-store'); self.send_header('Content-Length', str(len(body)))
        self.end_headers(); self.wfile.write(body)
class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True; allow_reuse_address = True
print('simulator at http://127.0.0.1:%d' % port, flush=True)
S(('127.0.0.1', port), H).serve_forever()
