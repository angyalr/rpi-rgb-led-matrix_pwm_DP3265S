#!/usr/bin/env python3
"""Password-protected web editor for the Pecsi Merlegstudio LED display settings."""

import argparse
import base64
import hmac
import html
import os
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

from config_store import CONFIG_PATH, PROFILES, apply_profile, load_config, save_config


PANEL_TYPES = ["", "dp3265s", "sm16269s", "FM6126A", "FM6127", "FM6373",
               "FM6363", "ICND1065L", "SM16380SH"]

COLOR_NAMES = [
    "Fekete", "Piros", "Zöld", "Kék",
    "Lila", "Ciánkék", "Sárga", "Fehér",
]

DISPLAY_FIELDS = ("display", "Kijelzo", [
    ("panel_type", "Panel tipus", "datalist", PANEL_TYPES),
    ("rows", "Sorok", "number", (1, 256)),
    ("cols", "Oszlopok", "number", (1, 2048)),
    ("chain", "Lancolt panelek", "number", (1, 64)),
    ("parallel", "Parhuzamos lancok", "number", (1, 6)),
    ("gpio_mapping", "GPIO mapping", "text", None),
    ("slowdown_gpio", "GPIO slowdown", "select", ["0", "1", "2", "3", "4"]),
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
])

FIELDS = [
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
        ("brightness", "Uzemi fenyero klasszikus", "number", (0, 100)),
        ("szovegkell", "Szoveg kijelzes", "checkbox", None),
    ]),
    ("spwm", "S-PWM chip beallitasok", [
        ("dp3265s_reg08", "DP3265S REG08 IGAIN override", "text", None),
        ("dp3265s_reg09", "DP3265S REG09 override", "text", None),
        ("dp3265s_reg0a", "DP3265S REG0A override", "text", None),
        ("dp3265s_reg0b", "DP3265S REG0B override", "text", None),
        ("dp3265s_reg0c", "DP3265S REG0C override", "text", None),
        ("dp3265s_reg0d", "DP3265S REG0D override", "text", None),
        ("dp3265s_debug", "DP3265S debug kiiras", "checkbox", None),
        ("sm16269s_gain_percent", "SM16269S gain szazalek", "number", (0, 100)),
        ("sm16269s_gain", "SM16269S gain override", "text", None),
        ("sm16269s_cfg1", "SM16269S CFG1 override", "text", None),
        ("sm16269s_cfg2", "SM16269S CFG2 override", "text", None),
        ("sm16269s_debug", "SM16269S debug kiiras", "checkbox", None),
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
    ("restart", "SEFAG ujrainditas", [
        ("day(1-7)", "Nap 1-7", "number", (1, 7)),
        ("hour(0-23)", "Ora", "number", (0, 23)),
        ("min(0-59)", "Perc", "number", (0, 59)),
    ]),
    ("timeOut", "SEFAG kapcsolat", [
        ("message(sec.)", "Uzenet timeout sec", "number", (1, 3600)),
    ]),
    DISPLAY_FIELDS,
]

CHECKBOX_FIELDS = {
    (section, key)
    for section, _, fields in FIELDS
    for key, _, field_type, _ in fields
    if field_type == "checkbox"
}


def restart_display_service():
    command = ["systemctl", "--no-block", "restart", "runtext.service"]
    geteuid = getattr(os, "geteuid", None)
    if geteuid is not None and geteuid() != 0:
        command = ["sudo", "-n"] + command

    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        details = (result.stderr or result.stdout or "").strip()
        if details:
            raise RuntimeError("%s: %s" % (" ".join(command), details))
        raise RuntimeError("%s exited with status %d" %
                           (" ".join(command), result.returncode))

FIELD_HINTS = {
    ("display", "panel_type"): "Panel típusa, pl. dp3265s a DP3265S P4 panelhez.",
    ("display", "rows"): "Sorok száma a panelen; általában 32 vagy 64.",
    ("display", "cols"): "Oszlopok száma a panelen; ez a teljes szélességet adja meg.",
    ("display", "chain"): "Sorba kapcsolt panelek száma; növeli a kimeneti szélességet.",
    ("display", "parallel"): "Párhuzamos kimenetek száma; több párhuzamos lánc nagyobb teljesítményhez.",
    ("display", "gpio_mapping"): "GPIO mapping név, például adafruit-hat vagy regular.",
    ("display", "slowdown_gpio"): "GPIO lassítás mértéke; ha a képernyő villog, emeld az értéket.",
    ("display", "brightness"): "Kezdeti fényerő százalékban (1-100).",
    ("display", "pwm_bits"): "PWM bitek; több bit több szürkeárnyalat, de nagyobb terhelés.",
    ("display", "pwm_lsb_nanoseconds"): "PWM LSB lépésideje nanomásodpercben; a PWM időzítés finomhangolása.",
    ("display", "pwm_dither_bits"): "Dither bitek; csökkentik a PWM sávosodását.",
    ("display", "scan_mode"): "Alapvető scan mód; 0 vagy 1 a kijelző típusa szerint.",
    ("display", "row_address_type"): "Sor címezés típusa; a paneled logicája dönti el a helyes értéket.",
    ("display", "spwm_row_address_type"): "S-PWM sor címezés; DP3265S esetén különösen fontos.",
    ("display", "spwm_scan"): "S-PWM scan sor mennyisége; általában 0-256 közötti érték.",
    ("display", "multiplexing"): "Multiplexelés szintje; magasabb érték csökkentheti a fényerőt.",
    ("display", "limit_refresh_hz"): "Maximális frissítési frekvencia; alacsonyabb érték stabilabb lehet.",
    ("display", "rgb_sequence"): "A LED-ek színbetű sorrendje a hardveres bekötéshez.",
    ("display", "pixel_mapper"): "Speciális pixel-kiosztás kezelése név alapján.",
    ("display", "show_refresh"): "Frissítési információ kiírása a naplóba.",
    ("display", "no_busy_waiting"): "Ha be van kapcsolva, nem blokkolja a CPU-t a várakozás.",
    ("display", "no_hardware_pulse"): "Hardveres impulzusok helyett szoftveres pulzusvezérlés.",
    ("display", "drop_privileges"): "Jogosultságcsökkentés a futás közben.",
    ("serial", "devicename"): "Soros port neve, pl. /dev/serial0 vagy /dev/ttyUSB0.",
    ("serial", "baudrate"): "Soros port sebessége bit/másodpercben.",
    ("serial", "parity"): "Paritás beállítása: 0=nincs, 1=odd, 2=even.",
    ("serial", "databits"): "Adatbitek száma a soros kommunikációban.",
    ("serial", "stopbits"): "Stop bitek száma a soros protokollban.",
    ("serial", "lekerdez"): "Ha be van kapcsolva, a mérleg lekérdezése engedélyezett.",
    ("scale", "start"): "A mérleg lekérdezésének kezdőkaraktere vagy parancsa.",
    ("scale", "start2"): "Második kezdési szakasz, ha a mérleg kétlépcsős.",
    ("scale", "end"): "A lekérdezés befejező karaktere vagy parancsa.",
    ("scale", "offset"): "Adatfájlban a súlyt tartalmazó byte eltolása.",
    ("scale", "length"): "A súly adat hosszúsága byte-ban.",
    ("scale", "kerdes"): "Mérleg lekérdező üzenete.",
    ("scale", "nullazas"): "Nullázó üzenet, amelyet a mérlegnek küld a tare-hoz.",
    ("colors", "uponzero"): "Színindex nulla súly esetén.",
    ("colors", "100below"): "Színindex, ha az érték 100 alatt van.",
    ("colors", "over100"): "Színindex, ha az érték 100 fölött van.",
    ("colors", "over40000"): "Színindex, ha az érték 40000 fölött van.",
    ("colors", "brightness"): "Fényerő 0-100 tartományban. DP3265S-nél 100 a REG08 IGAIN maximum, a korábbi alap kb. 75.",
    ("colors", "szovegkell"): "Ha be van kapcsolva, szöveg is megjelenik a kijelzőn.",
    ("spwm", "dp3265s_reg08"): "DP3265S REG08 IGAIN regiszter egyéni értéke. Maximalis teszthez 0x08FF.",
    ("spwm", "dp3265s_reg09"): "DP3265S REG09 regiszter egyéni értéke.",
    ("spwm", "dp3265s_reg0a"): "DP3265S REG0A regiszter egyéni értéke.",
    ("spwm", "dp3265s_reg0b"): "DP3265S REG0B regiszter egyéni értéke.",
    ("spwm", "dp3265s_reg0c"): "DP3265S REG0C regiszter egyéni értéke.",
    ("spwm", "dp3265s_reg0d"): "DP3265S REG0D regiszter egyéni értéke.",
    ("spwm", "dp3265s_debug"): "Bekapcsolva a program indulaskor kiirja a DP3265S regiszterertekeket.",
    ("spwm", "sm16269s_gain_percent"): "SM16269S 6 bites aramerosites 0-100 tartomanyban. Uresen a fenyero beallitasbol szamolodik.",
    ("spwm", "sm16269s_gain"): "SM16269S Current Gain szo egyeni erteke, LE=3. Maximalis teszthez 0x003f.",
    ("spwm", "sm16269s_cfg1"): "SM16269S konfiguracio 1 szo, LE=5. Uresen a beepitett alap: 0x1810.",
    ("spwm", "sm16269s_cfg2"): "SM16269S konfiguracio 2 szo, LE=7. Uresen a beepitett alap: 0x3ce0.",
    ("spwm", "sm16269s_debug"): "Bekapcsolva a program indulaskor kiirja az SM16269S regisztererteket.",
    ("simulator", "active"): "Szimulátor használata valódi mérleg helyett.",
    ("simulator", "devicename"): "A szimulátor eszközének neve.",
    ("simulator", "baudrate"): "Szimulátor soros port sebessége.",
    ("simulator", "parity"): "Szimulátor paritás beállítása.",
    ("simulator", "databits"): "Szimulátor adatbitek száma.",
    ("simulator", "stopbits"): "Szimulátor stop bitek száma.",
    ("simulator", "protocol"): "A szimulátor által használt protokoll neve.",
    ("simulator", "onalloadas"): "Ha engedélyezett, automatikusan küldi a szimulált adatokat.",
    ("simulator", "msaddress"): "Merleg cím a szimulátoros kommunikációhoz.",
    ("sensor", "ds1307"): "RTC óra hasznosítása, ha van DS1307 modul.",
    ("sensor", "ds18b20"): "DS18B20 hőmérséklet-érzékelő használata.",
    ("ethernet", "client"): "Ethernet kliens mód engedélyezése.",
    ("ethernet", "request"): "Kérések fogadása Etherneten keresztül.",
    ("ethernet", "server ip"): "A távoli szerver IP címe.",
    ("ethernet", "server port"): "A távoli szerver portja.",
    ("restart", "day(1-7)"): "A hét napja, amikor újraindul a rendszer.",
    ("restart", "hour(0-23)"): "Az újraindítás órája.",
    ("restart", "min(0-59)"): "Az újraindítás perce.",
    ("timeOut", "message(sec.)"): "Üzenet timeout másodpercben.",
}


def value(config, section, key):
    return config.get(section, key, fallback="")


def field_html(config, section, key, label, field_type, options):
    current = value(config, section, key)
    name = section + "." + key
    escaped_name = html.escape(name, quote=True)
    escaped_value = html.escape(current, quote=True)
    hint_text = FIELD_HINTS.get((section, key), "")
    title_attr = ' title="%s"' % html.escape(hint_text, quote=True) if hint_text else ""
    if field_type == "checkbox":
        checked = " checked" if current.lower() in ("1", "true", "yes", "on") else ""
        control = '<input type="checkbox" name="%s" value="true"%s%s>' % (
            escaped_name, checked, title_attr)
    elif field_type == "select":
        select_options = []
        for option in options:
            selected = " selected" if current == option else ""
            escaped_value = html.escape(option, quote=True)
            option_label = escaped_value
            if section == "colors":
                try:
                    option_label = html.escape(COLOR_NAMES[int(option)], quote=True)
                except (ValueError, IndexError):
                    option_label = escaped_value
            select_options.append('<option value="%s"%s>%s</option>' % (
                escaped_value, selected, option_label))
        control = '<select name="%s"%s>%s</select>' % (
            escaped_name, title_attr, "".join(select_options))
    elif field_type == "datalist":
        choices = "".join('<option value="%s"></option>' %
                          html.escape(option, quote=True) for option in options)
        control = ('<input name="%s" value="%s" list="panel-types"%s>'
                   '<datalist id="panel-types">%s</datalist>') % (
                       escaped_name, escaped_value, title_attr, choices)
    elif field_type == "number":
        minimum, maximum = options
        control = ('<input type="number" name="%s" value="%s" min="%d" max="%d"%s>'
                   % (escaped_name, escaped_value, minimum, maximum, title_attr))
    else:
        control = '<input name="%s" value="%s"%s>' % (
            escaped_name, escaped_value, title_attr)
    hint_html = ('<small class="hint">%s</small>' % html.escape(hint_text)) if hint_text else ""
    return '<label><span>%s</span>%s%s</label>' % (
        html.escape(label), control, hint_html)


def profile_form_html(config):
    current_profile = config.get("app", "profile", fallback="")
    current_script = config.get("app", "script", fallback="ms")
    options = []
    for profile in PROFILES:
        selected = " selected" if profile["id"] == current_profile else ""
        options.append('<option value="%s"%s>%s</option>' % (
            html.escape(profile["id"], quote=True),
            selected,
            html.escape(profile["label"])))
    return """
    <form class="profile-form" method="post" action="/profile">
      <section>
        <h2>Gyors profil</h2>
        <div class="profile-row">
          <label><span>Kijelzo es kapcsolat</span><select name="profile">%s</select></label>
          <div class="profile-meta">Indulo program: <code>%s/runtext.py</code></div>
          <button type="submit">Profil betoltese</button>
        </div>
      </section>
    </form>""" % ("".join(options), html.escape(current_script))


def render_page(config, saved=False):
    sections = []
    for section, title, fields in FIELDS:
        controls = "".join(field_html(config, section, *field) for field in fields)
        sections.append(
            '<section class="section-%s"><h2>%s</h2><div class="grid">%s</div></section>' %
            (html.escape(section.replace(" ", "-"), quote=True),
             html.escape(title), controls))
    notice = '<div class="notice">Mentve. A kijelzo ujrainditasa elindult.</div>' if saved else ""
    path = html.escape(str(CONFIG_PATH))
    profile_form = profile_form_html(config)
    return """<!doctype html>
<html lang="hu">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>LED kijelzo beallitasok</title>
  <style>
    :root { --bg:#eef2f6; --surface:#fff; --line:#ccd4dd; --text:#16212d;
      --muted:#536170; --accent:#0875bc; --success:#e7f5ec; --success-text:#185d37; }
    * { box-sizing:border-box; }
    body { margin:0; color:var(--text); background:var(--bg);
      font:14px Arial, sans-serif; letter-spacing:0; }
    header { background:#182937; color:#fff; padding:14px 22px; }
    header h1 { max-width:1440px; margin:0 auto; font-size:20px; font-weight:600; }
    main { max-width:1440px; margin:0 auto; padding:12px 18px 28px; }
    .toolbar { display:flex; gap:16px; align-items:center; justify-content:space-between;
      margin-bottom:10px; color:var(--muted); }
    .notice { background:var(--success); color:var(--success-text); border:1px solid #b7dec7;
      padding:8px 10px; margin-bottom:10px; border-radius:6px; }
    section { background:var(--surface); border-top:1px solid rgba(90,105,120,.22); padding:10px 0; }
    section:first-of-type { border-top:0; }
    section h2 { font-size:15px; margin:0 12px 8px; font-weight:600; }
    .section-serial { background:#eef8f2; }
    .section-scale { background:#fff7e6; }
    .section-colors { background:#f3f0ff; }
    .section-spwm { background:#eaf7fb; }
    .section-simulator { background:#fff0f2; }
    .section-sensor { background:#f2f7e8; }
    .section-ethernet { background:#edf4ff; }
    .section-restart { background:#f8f1e8; }
    .section-timeOut { background:#f1f5f9; }
    .section-display { background:#f5f2ea; }
    form { background:var(--surface); border:1px solid var(--line); border-radius:6px; overflow:hidden; }
    .profile-form { margin-bottom:10px; }
    .profile-row { display:grid; grid-template-columns:minmax(320px, 1fr) auto auto;
      gap:8px 12px; align-items:center; padding:0 12px 10px; }
    .profile-meta { color:var(--muted); white-space:nowrap; }
    .grid { display:grid; grid-template-columns:repeat(5, minmax(210px, 1fr));
      gap:6px 8px; padding:0 12px; }
    label { min-width:0; display:grid; grid-template-columns:92px minmax(90px, 1fr);
      gap:5px 6px; align-items:center; color:var(--muted); }
    label span { font-size:13px; line-height:1.2; }
    input, select { width:100%%; height:30px; border:1px solid var(--line); border-radius:4px;
      background:rgba(255,255,255,.92); padding:0 7px; color:var(--text); font-size:13px; }
    input[type=checkbox] { width:18px; height:18px; margin:0; justify-self:start; }
    .hint { display:none; }
    .actions { display:flex; justify-content:flex-end; gap:12px; border-top:1px solid var(--line);
      padding:10px 12px; margin-top:10px; }
    button { background:var(--accent); border:0; border-radius:4px; color:#fff;
      font-size:13px; height:34px; padding:0 18px; cursor:pointer; }
    code { color:var(--muted); }
    @media (max-width:1180px) {
      main { padding:10px; }
      .grid { grid-template-columns:repeat(3, minmax(220px, 1fr)); }
    }
    @media (max-width:850px) {
      .grid { grid-template-columns:repeat(2, minmax(220px, 1fr)); }
      .profile-row { grid-template-columns:1fr; align-items:stretch; }
      .profile-meta { padding-bottom:0; white-space:normal; }
    }
    @media (max-width:640px) {
      .grid { grid-template-columns:1fr; }
      label { grid-template-columns:1fr; gap:4px; }
    }
  </style>
</head>
<body>
  <header><h1>LED kijelzo beallitasok</h1></header>
  <main>
    <div class="toolbar"><span>Konfiguracio: <code>%s</code></span></div>
    %s
    %s
    <form id="save-form" method="post" action="/save">
      %s
    </form>
    <div class="actions">
      <button type="submit" form="save-form">Mentes</button>
    </div>
  </main>
</body>
</html>""" % (path, notice, profile_form, "".join(sections))


def apply_form(config, params):
    for section, _, fields in FIELDS:
        for key, _, field_type, options in fields:
            name = section + "." + key
            if field_type == "checkbox":
                config.set(section, key, "true" if name in params else "false")
                continue
            submitted = params.get(name, [""])[0].strip()
            if field_type == "number":
                # Skip empty number fields - keep the existing value
                if not submitted:
                    continue
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
        if self.path not in ("/save", "/profile", "/restart"):
            self.send_error(404)
            return

        if self.path == "/restart":
            try:
                restart_display_service()
                self.send_page(saved=True)
            except (OSError, RuntimeError) as error:
                self.send_error(500, "Ujrainditas sikertelen: %s" % str(error))
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            params = parse_qs(self.rfile.read(length).decode("utf-8"),
                              keep_blank_values=True)
            config = load_config()
            if self.path == "/profile":
                profile_id = params.get("profile", [""])[0]
                if not apply_profile(config, profile_id):
                    raise ValueError("Unknown profile")
            else:
                apply_form(config, params)
            save_config(config)
            if self.path == "/save":
                restart_display_service()
        except (OSError, RuntimeError, ValueError) as error:
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
