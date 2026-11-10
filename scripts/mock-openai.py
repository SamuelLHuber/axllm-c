#!/usr/bin/env python3
"""Minimal OpenAI chat completions mock for offline inference smoke tests."""
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def answer_text(request):
    if request.get("response_format") or request.get("text", {}).get("format"):
        return json.dumps({"answer": "Paris"})
    return "Answer: Paris"


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        request = json.loads(self.rfile.read(length) or b"{}")
        if not self.path.endswith("/chat/completions"):
            self.send_error(404)
            return
        text = answer_text(request)
        model = request.get("model", "mock")
        usage = {"prompt_tokens": 10, "completion_tokens": 3, "total_tokens": 13}
        if request.get("stream"):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.end_headers()
            for i in range(0, len(text), 4):
                chunk = {"id": "mock", "object": "chat.completion.chunk", "model": model,
                         "choices": [{"index": 0, "delta": {"content": text[i:i + 4]}, "finish_reason": None}]}
                self.wfile.write(f"data: {json.dumps(chunk)}\n\n".encode())
            done = {"id": "mock", "object": "chat.completion.chunk", "model": model,
                    "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}], "usage": usage}
            self.wfile.write(f"data: {json.dumps(done)}\n\ndata: [DONE]\n\n".encode())
            return
        body = json.dumps({
            "id": "mock", "object": "chat.completion", "model": model,
            "choices": [{"index": 0, "message": {"role": "assistant", "content": text}, "finish_reason": "stop"}],
            "usage": usage,
        }).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    print(server.server_address[1], flush=True)
    sys.stdout.close()
    server.serve_forever()
