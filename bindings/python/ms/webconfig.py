#!/usr/bin/env python3
"""Password-protected web editor for the Metrisoft LED display settings."""

import argparse
import base64
import hmac
import html
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

from config_store import CONFIG_PATH, load_config, save_config


PANEL_TYPES = ["", "dp3265s", "FM6126A", "FM6127", "FM6373",
               "FM6363", "ICND1065L", "SM16380SH"]

FIELDS = [
    ("display", "Kijelzo", [
        ("panel_type", "Panel tipus", "datalist", PANEL_TYPES),
        ("rows", "Sorok", "number", (1, 256)),
        ("cols", "Oszlopok", "number", (1, 2048)),
        ("chain", "Lancolt panelek", "number", (1, 64)),
        ("parallel", "Parhuzamos lancok", "number", (1, 6)),
        ("gpio_mapping", "GPIO mapping", "text", None),
        ("slowdown_gpio", "GPIO slowdown", "select", ["0", "1", "2", "3", "4"]),
        ("brightness", "Kezdo fenyero", "number", (1, 100)),
        ("pwm_bits", "PWM bitek", "number", (1, 11)),
        ("pwm_lsb_nanoseconds", "PWM LSB ns", "number", (1, 10000)),
        ("pwm_dither_bits", "PWM dither bitek", "number", (0, 8)),
        ("scan_mode", "Scan mode", "select", ["0", "1"]),
        ("row_address_type", "Sor cimzes", "select", ["0", "1", "2", "3", "4", "5"]),
        ("spwm_row_address_type", "S-PWM sor cimzes", "select", ["0", "1", "2"]),
        ("spwm_scan", "S-PWM scan sor", "number", (0, 256)),
        ("multiplexing", "Multiplexing", "number", (0, 64)),
        ("limit_refresh_hz", "Max frissites Hz", "number", (0, 10000)),
        ("rgb_sequence", "RGB sorrend", "text", None),
        ("pixel_mapper", "Pixel mapper", "text", None),
        ("show_refresh", "Refresh kiiras", "checkbox", None),
        ("no_busy_waiting", "Busy wait tiltasa", "checkbox", None),
        ("no_hardware_pulse", "Hardware pulse tiltasa", "checkbox", None),
        ("drop_privileges", "Jogosultsag eldobasa", "checkbox", None),
    ]),
    ("serial", "Merleg soros port", [
        ("devicename", "Eszkoz", "text", None),
        ("baudrate", "Baudrate", "select", ["1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"]),
        ("parity", "Paritas", "select", ["0", "1", "2"]),
        ("databits", "Adatbitek", "select", ["7", "8"]),
        ("stopbits", "Stopbitek", "select", ["1", "2"]),
        ("lekerdez", "Lekerdezes", "checkbox", None),
    ]),
    ("scale", "Merleg protokoll", [
        ("start", "Start", "text", None),
        ("start2", "Start 2", "text", None),
        ("end", "Vege", "text", None),
        ("offset", "Offset", "number", (0, 64)),
        ("length", "Hossz", "number", (1, 64)),
        ("kerdes", "Lekerdezo uzenet", "text", None),
        ("nullazas", "Nullazo uzenet", "text", None),
    ]),
    ("colors", "Megjelenes", [
        ("uponzero", "Nulla szin", "select", ["0", "1", "2", "3", "4", "5", "6", "7"]),
        ("100below", "100 alatt szin", "select", ["0", "1", "2", "3", "4", "5", "6", "7"]),
        ("over100", "100 felett szin", "select", ["0", "1", "2", "3", "4", "5", "6", "7"]),
        ("over40000", "40000 felett szin", "select", ["0", "1", "2", "3", "4", "5", "6", "7"]),
        ("brightness", "Uzemi fenyero", "number", (1, 10)),
        ("szovegkell", "Szoveg kijelzes", "checkbox", None),
    ]),
    ("simulator", "Szimulator", [
        ("active", "Aktiv", "checkbox", None),
        ("devicename", "Eszkoz", "text", None),
        ("baudrate", "Baudrate", "number", (1, 1000000)),
        ("parity", "Paritas", "select", ["0", "1", "2"]),
        ("databits", "Adatbitek", "select", ["7", "8"]),
        ("stopbits", "Stopbitek", "select", ["1", "2"]),
        ("protocol", "Protokoll", "text", None),
        ("onalloadas", "Onallo adas", "checkbox", None),
        ("msaddress", "MS cim", "text", None),
    ]),
    ("sensor", "Szenzorok", [
        ("ds1307", "DS1307", "checkbox", None),
        ("ds18b20", "DS18B20", "checkbox", None),
    ]),
    ("ethernet", "Ethernet merleg kapcsolat", [
        ("client", "Kliens mod", "checkbox", None),
        ("request", "Lekerdezo mod", "checkbox", None),
        ("server ip", "Szerver IP", "text", None),
        ("server port", "Szerver port", "number", (1, 65535)),
    ]),
]

CHECKBOX_FIELDS = {
    (section, key)
    for section, _, fields in FIELDS
    for key, _, field_type, _ in fields
    if field_type == "checkbox"
}


def value(config, section, key):
    return config.get(section, key, fallback="")


def field_html(config, section, key, label, field_type, options):
    current = value(config, section, key)
    name = section + "." + key
    escaped_name = html.escape(name, quote=True)
    escaped_value = html.escape(current, quote=True)
    if field_type == "checkbox":
        checked = " checked" if current.lower() in ("1", "true", "yes", "on") else ""
        control = '<input type="checkbox" name="%s" value="true"%s>' % (
            escaped_name, checked)
    elif field_type == "select":
        select_options = []
        for option in options:
            selected = " selected" if current == option else ""
            escaped = html.escape(option, quote=True)
            select_options.append('<option value="%s"%s>%s</option>' % (
                escaped, selected, escaped))
        control = '<select name="%s">%s</select>' % (
            escaped_name, "".join(select_options))
    elif field_type == "datalist":
        choices = "".join('<option value="%s"></option>' %
                          html.escape(option, quote=True) for option in options)
        control = ('<input name="%s" value="%s" list="panel-types">'
                   '<datalist id="panel-types">%s</datalist>') % (
                       escaped_name, escaped_value, choices)
    elif field_type == "number":
        minimum, maximum = options
        control = ('<input type="number" name="%s" value="%s" min="%d" max="%d">'
                   % (escaped_name, escaped_value, minimum, maximum))
    else:
        control = '<input name="%s" value="%s">' % (escaped_name, escaped_value)
    return '<label><span>%s</span>%s</label>' % (html.escape(label), control)


def render_page(config, saved=False):
    sections = []
    for section, title, fields in FIELDS:
        controls = "".join(field_html(config, section, *field) for field in fields)
        sections.append('<section><h2>%s</h2><div class="grid">%s</div></section>' %
                        (html.escape(title), controls))
    notice = '<div class="notice">Mentve. A futasi beallitasok ujrainditas utan lepnek eletbe.</div>' if saved else ""
    path = html.escape(str(CONFIG_PATH))
    return """<!doctype html>
<html lang="hu">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>LED kijelzo beallitasok</title>
  <style>
    :root { --bg:#f4f6f8; --surface:#fff; --line:#ccd4dd; --text:#16212d;
      --muted:#536170; --accent:#0875bc; --success:#e7f5ec; --success-text:#185d37; }
    * { box-sizing:border-box; }
    body { margin:0; color:var(--text); background:var(--bg);
      font:14px Arial, sans-serif; letter-spacing:0; }
    header { background:#182937; color:#fff; padding:18px 24px; }
    header h1 { max-width:1120px; margin:0 auto; font-size:22px; font-weight:600; }
    main { max-width:1120px; margin:0 auto; padding:18px 24px 36px; }
    .toolbar { display:flex; gap:16px; align-items:center; justify-content:space-between;
      margin-bottom:16px; color:var(--muted); }
    .notice { background:var(--success); color:var(--success-text); border:1px solid #b7dec7;
      padding:10px 12px; margin-bottom:16px; border-radius:6px; }
    section { background:var(--surface); border-top:1px solid var(--line); padding:16px 0; }
    section:first-of-type { border-top:0; }
    section h2 { font-size:16px; margin:0 16px 12px; font-weight:600; }
    form { background:var(--surface); border:1px solid var(--line); border-radius:6px; overflow:hidden; }
    .grid { display:grid; grid-template-columns:repeat(4, minmax(150px, 1fr));
      gap:12px 16px; padding:0 16px; }
    label { min-width:0; display:flex; flex-direction:column; gap:5px; color:var(--muted); }
    input, select { width:100%; height:36px; border:1px solid var(--line); border-radius:4px;
      background:#fff; padding:0 9px; color:var(--text); font-size:14px; }
    input[type=checkbox] { width:20px; height:20px; margin-top:6px; }
    .actions { display:flex; justify-content:flex-end; border-top:1px solid var(--line);
      padding:14px 16px; margin-top:16px; }
    button { background:var(--accent); border:0; border-radius:4px; color:#fff;
      font-size:14px; height:38px; padding:0 20px; cursor:pointer; }
    code { color:var(--muted); }
    @media (max-width:850px) { .grid { grid-template-columns:repeat(2, minmax(130px, 1fr)); } }
    @media (max-width:520px) { main { padding:12px; } .grid { grid-template-columns:1fr; } }
  </style>
</head>
<body>
  <header><h1>LED kijelzo beallitasok</h1></header>
  <main>
    <div class="toolbar"><span>Konfiguracio: <code>%s</code></span></div>
    %s
    <form method="post" action="/save">
      %s
      <div class="actions"><button type="submit">Mentes</button></div>
    </form>
  </main>
</body>
</html>""" % (path, notice, "".join(sections))


def apply_form(config, params):
    for section, _, fields in FIELDS:
        for key, _, field_type, options in fields:
            name = section + "." + key
            if field_type == "checkbox":
                config.set(section, key, "true" if name in params else "false")
                continue
            submitted = params.get(name, [""])[0].strip()
            if field_type == "number":
                minimum, maximum = options
                number = int(submitted)
                if number < minimum or number > maximum:
                    raise ValueError("%s is out of range" % name)
                submitted = str(number)
            config.set(section, key, submitted)


class ConfigurationHandler(BaseHTTPRequestHandler):
    server_version = "MsConfig/1.0"

    def authenticated(self):
        header = self.headers.get("Authorization", "")
        if not header.startswith("Basic "):
            return False
        try:
            entered = base64.b64decode(header[6:]).decode("utf-8")
        except (ValueError, UnicodeDecodeError):
            return False
        required = self.server.username + ":" + self.server.password
        return hmac.compare_digest(entered, required)

    def require_auth(self):
        self.send_response(401)
        self.send_header("WWW-Authenticate", 'Basic realm="LED configuration"')
        self.end_headers()

    def send_page(self, saved=False, status=200):
        payload = render_page(load_config(), saved=saved).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if not self.authenticated():
            self.require_auth()
            return
        if self.path != "/":
            self.send_error(404)
            return
        self.send_page()

    def do_POST(self):
        if not self.authenticated():
            self.require_auth()
            return
        if self.path != "/save":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            params = parse_qs(self.rfile.read(length).decode("utf-8"),
                              keep_blank_values=True)
            config = load_config()
            apply_form(config, params)
            save_config(config)
        except (OSError, ValueError) as error:
            self.send_error(400, str(error))
            return
        self.send_page(saved=True)

    def log_message(self, format_string, *args):
        print("[%s] %s" % (self.address_string(), format_string % args))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--username", default="admin")
    parser.add_argument("--password", default=os.environ.get("MS_WEB_PASSWORD"))
    args = parser.parse_args()
    if not args.password:
        parser.error("Set MS_WEB_PASSWORD or provide --password.")
    server = ThreadingHTTPServer((args.host, args.port), ConfigurationHandler)
    server.username = args.username
    server.password = args.password
    print("Configuration page: http://%s:%d" % (args.host, args.port))
    print("Configuration file:", CONFIG_PATH)
    server.serve_forever()


if __name__ == "__main__":
    main()
