from http.server import BaseHTTPRequestHandler, HTTPServer

class ThresholdHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/threshold":
            self.send_response(200)
            self.send_header("Content-type", "text/plain")
            self.end_headers()
            self.wfile.write(b"5")  # 👈 this is your threshold value
        else:
            self.send_response(404)
            self.end_headers()

server_address = ("0.0.0.0", 8080)  # Serves on your machine's IP
httpd = HTTPServer(server_address, ThresholdHandler)
print("Serving on port 80...")
httpd.serve_forever()
