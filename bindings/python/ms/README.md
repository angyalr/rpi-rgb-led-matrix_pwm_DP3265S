# Pecsi Merlegstudio LED application

The display application and its web configuration page share:

```text
/home/pi/runtext.ini
```

After copying this version to the Raspberry Pi, rebuild the Python binding
once because the display options now also expose the S-PWM scan setting:

```bash
.venv/bin/python -m pip install --no-cache-dir --force-reinstall .
```

Copy `runtext.ini` there once, leave it writable by the configuration server,
then start the display:

```bash
cp bindings/python/ms/runtext.ini /home/pi/runtext.ini
chown pi:pi /home/pi/runtext.ini
sudo .venv/bin/python bindings/python/ms/runtext_launcher.py
```

For automatic startup, install the launcher based display service:

```bash
sudo cp bindings/python/ms/runtext.service /etc/systemd/system/runtext.service
sudo systemctl daemon-reload
sudo systemctl enable --now runtext.service
```

The service starts `bindings/python/ms/runtext_launcher.py`; the launcher reads
the selected web profile from `/home/pi/runtext.ini` and then starts either the
`ms` or the `sefag` display application.

Start the configuration page on the local network:

```bash
export MS_WEB_PASSWORD='choose-a-password'
.venv/bin/python bindings/python/ms/webconfig.py
```

Open `http://<raspberry-pi-ip>:8080` and sign in as `admin` with that
password. The save button writes the INI file and restarts the display service.

The save button runs this command without waiting for the service to finish
starting:

```bash
sudo -n systemctl --no-block restart runtext.service
```

If the web configuration page runs as the `pi` user, allow only that command
without a password:

```bash
echo 'pi ALL=(root) NOPASSWD: /usr/bin/systemctl --no-block restart runtext.service' | sudo tee /etc/sudoers.d/pm-led-web-restart
sudo chmod 440 /etc/sudoers.d/pm-led-web-restart
```

The command-line LED arguments remain available and override values loaded
from the INI file for that single run.
