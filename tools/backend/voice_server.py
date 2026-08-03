#!/usr/bin/env python3
# ===========================================================================
#  voice_server.py  —  Voice Journal backend stub (VJ·R1)
# ===========================================================================
#  A local Python script that:
#    - Runs on your PC (`python tools/backend/voice_server.py`)
#    - Listens on `http://localhost:8000/voice` (POST only)
#    - Accepts a WAV file in the request body
#    - Uses `whisper` (pip install openai-whisper) to transcribe it
#    - Returns `{"status":"ok","text":"...","title":"..."}`
#    - Includes a simple `--help`
# ===========================================================================
#  Requires only `openai-whisper` and `flask` (add pip install commands to top comment)
#  pip install openai-whisper flask
# ===========================================================================

import argparse
import os
import sys
from flask import Flask, request, jsonify
import whisper

app = Flask(__name__)

# Load Whisper model (tiny by default for speed)
model = None

def load_model():
    global model
    try:
        model = whisper.load_model("tiny")
        print("Whisper model loaded successfully")
    except Exception as e:
        print(f"Error loading Whisper model: {e}")
        print("Make sure you have installed openai-whisper: pip install openai-whisper")
        sys.exit(1)

@app.route('/voice', methods=['POST'])
def handle_voice():
    if not request.data:
        return jsonify({"status": "error", "message": "No WAV data received"}), 400
    
    # Save the WAV data to a temporary file
    temp_wav = "temp_voice.wav"
    try:
        with open(temp_wav, "wb") as f:
            f.write(request.data)
        
        # Transcribe using Whisper
        result = model.transcribe(temp_wav)
        
        # Generate title from first few words of transcription
        text = result["text"].strip()
        if len(text) > 0:
            words = text.split()[:5]
            title = " ".join(words)
            if len(title) > 48:
                title = title[:45] + "..."
        else:
            title = "Voice recording"
        
        # Clean up temp file
        os.remove(temp_wav)
        
        return jsonify({
            "status": "ok",
            "text": text,
            "title": title
        })
        
    except Exception as e:
        if os.path.exists(temp_wav):
            os.remove(temp_wav)
        print(f"Error processing voice: {e}")
        return jsonify({"status": "error", "message": str(e)}), 500

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Voice Journal backend server')
    parser.add_argument('--host', default='127.0.0.1', help='Host to bind to (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, default=8000, help='Port to bind to (default: 8000)')
    parser.add_argument('--help', action='store_true', help='Show this help message')
    
    args = parser.parse_args()
    
    if args.help:
        parser.print_help()
        sys.exit(0)
    
    print(f"Starting Voice Journal backend server on {args.host}:{args.port}")
    print("Press Ctrl+C to stop")
    
    # Load Whisper model
    load_model()
    
    # Run Flask app
    app.run(host=args.host, port=args.port, debug=False)
