# Metrisoft LED application

The display application and its web configuration page share:

```text
/home/pi/runtext.ini
```

Copy `runtext.ini` there once, then start the display:

```bash
sudo .venv/bin/python bindings/python/ms/runtext.py
```

Start the configuration page on the local network:

```bash
export MS_WEB_PASSWORD='choose-a-password'
.venv/bin/python bindings/python/ms/webconfig.py
```

Open `http://<raspberry-pi-ip>:8080` and sign in as `admin` with that
password. Saved settings take effect the next time `runtext.py` starts.

The command-line LED arguments remain available and override values loaded
from the INI file for that single run.
