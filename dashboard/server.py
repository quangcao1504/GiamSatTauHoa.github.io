"""Tong hop du lieu tau -> trang web cho Truong tau / Tram / Tieu vien.
Chay (laptop da bat Mosquitto, ESP32 gui ve IP laptop):
    pip install paho-mqtt
    python server.py
Mo: http://localhost:8080   (dien thoai cung WiFi: http://IP_LAPTOP:8080)
"""
import json, math, os, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import paho.mqtt.client as mqtt

BROKER, MQTT_PORT, WEB_PORT = "localhost", 1883, 8080
SPEED_LIMIT_KMH = 80
OFFLINE_S = 5                                    # qua 5 giay khong co du lieu = mat ket noi
STATIONS = [("Ga A", 21.0245, 105.8412),         # SUA thanh toa do ga that
            ("Ga B", 21.0300, 105.8500)]

trains, lock = {}, threading.Lock()
tracks = {}                                       # id -> [[lat, lon, km/h], ...]
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hanh_trinh.csv")
_new = not os.path.exists(LOG)
logf = open(LOG, "a", encoding="utf-8")
if _new:
    logf.write("pc_time,id,lat,lon,speed_kmh,ax,ay,az,status\n")


def dist_m(la1, lo1, la2, lo2):
    p = math.pi / 180
    h = (math.sin((la2 - la1) * p / 2) ** 2 +
         math.cos(la1 * p) * math.cos(la2 * p) * math.sin((lo2 - lo1) * p / 2) ** 2)
    return 12742000 * math.asin(math.sqrt(h))


def on_connect(c, u, flags, rc, properties=None):
    c.subscribe("railway/train/+/telemetry")


def on_message(c, u, msg):
    try:
        d = json.loads(msg.payload)
    except Exception:
        return
    if "seq" not in d:
        return
    acc = d.get("acc", [0, 0, 0])
    mag = math.sqrt(sum(x * x for x in acc))
    speed = d.get("speed", 0) * 3.6                       # m/s -> km/h
    gps = d.get("status", "no_gps")

    al = []
    if gps == "no_gps": al.append("Mất tín hiệu GPS")
    elif gps == "warn": al.append("GPS yếu")
    if speed > SPEED_LIMIT_KMH: al.append(f"Quá tốc độ {SPEED_LIMIT_KMH} km/h")
    if d.get("fwd", 0) < -1.5: al.append("Phanh gấp")
    elif abs(mag - 9.81) > 2.0: al.append("Rung lắc mạnh")

    t = {"id": d["id"], "t": time.time(), "speed": round(speed, 1), "gps": gps,
         "sats": d.get("sats", 0), "rssi": d.get("rssi", 0), "mag": round(mag, 2),
         "alerts": al, "lat": d.get("lat", 0), "lon": d.get("lon", 0)}
    if gps != "no_gps":                                   # tinh ga gan nhat + thoi gian du kien
        name, la, lo = min(STATIONS, key=lambda s: dist_m(d["lat"], d["lon"], s[1], s[2]))
        dm = dist_m(d["lat"], d["lon"], la, lo)
        t.update(station=name, dist=round(dm), eta=round(dm / (speed / 3.6)) if speed > 2 else None)
    with lock:
        t["hist"] = (trains.get(d["id"], {}).get("hist", [])[-119:]) + [round(speed, 1)]
        trains[d["id"]] = t
        if gps != "no_gps" and (d.get("lat") or d.get("lon")):
            tr = tracks.setdefault(d["id"], [])
            if not tr or dist_m(tr[-1][0], tr[-1][1], d["lat"], d["lon"]) > 3:
                tr.append([d["lat"], d["lon"], round(speed, 1)])
                if len(tr) > 20000: tr.pop(0)
        logf.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')},{d['id']},{d.get('lat')},{d.get('lon')},"
                   f"{round(speed, 1)},{acc[0]},{acc[1]},{acc[2]},{gps}\n")
        logf.flush()


def state():
    now = time.time()
    with lock:
        out = []
        for t in trains.values():
            x = dict(t)
            x["online"] = now - t["t"] < OFFLINE_S
            if not x["online"]:
                x["alerts"] = ["Mất kết nối với tàu"]
            out.append(x)
    return out


def track_json():
    with lock:
        out = {}
        for k, tr in tracks.items():
            pts = tr[::max(1, len(tr) // 1500)]       # toi da ~1500 diem/tau
            if tr and pts[-1] is not tr[-1]:
                pts = pts + [tr[-1]]
            out[k] = pts
    return {"stations": STATIONS, "tracks": out}


class H(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/api/state":
            body, ctype = json.dumps(state()).encode(), "application/json"
        elif self.path == "/api/track":
            body, ctype = json.dumps(track_json()).encode(), "application/json"
        elif self.path == "/world.json":
            with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "world.json"), "rb") as f:
                body, ctype = f.read(), "application/json"
        else:
            with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "dashboard.html"), "rb") as f:
                body, ctype = f.read(), "text/html; charset=utf-8"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


try:
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="dashboard_server")
except AttributeError:
    client = mqtt.Client(client_id="dashboard_server")
client.on_connect, client.on_message = on_connect, on_message
client.connect(BROKER, MQTT_PORT, 30)
client.loop_start()
print(f"Dashboard: http://localhost:{WEB_PORT}")
ThreadingHTTPServer(("0.0.0.0", WEB_PORT), H).serve_forever()