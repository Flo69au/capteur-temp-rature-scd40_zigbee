#!/usr/bin/env python3
"""Query Open-Meteo elevation API and print altitude in meters (integer).
Usage: python fetch_altitude.py <latitude> <longitude>
"""
import sys
import json

def fetch_altitude(lat, lon):
    try:
        from urllib.request import urlopen
        url = f"https://api.open-meteo.com/v1/elevation?latitude={lat}&longitude={lon}"
        with urlopen(url, timeout=5) as resp:
            data = json.loads(resp.read())
        return int(round(data["elevation"][0]))
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: fetch_altitude.py <latitude> <longitude>", file=sys.stderr)
        sys.exit(1)
    print(fetch_altitude(sys.argv[1], sys.argv[2]))
