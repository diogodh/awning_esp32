# Automatic awning (Gaviota Elite + ESP32)

Controls a balcony awning (**Gaviota Elite** motor, ref. **60090165**) from an ESP32:

- **forecast-driven automation**: retracts when it's windy, extends when it rains;
- **night mode**: at night it only moves when it really has to, to keep the noise down;
- **local page** (`http://toldo.local`) and **public page** (`https://toldo.exemplo.com`) showing the state and history, with UP / STOP / DOWN buttons and settings.

> This repository **contains no passwords, usernames or keys**. Credentials live in `esp32/toldo_meteo/segredos.h`, which is in `.gitignore` (see section 6). `toldo.exemplo.com` is a placeholder domain: use your own.
>
> The code, its comments and the web pages are in Portuguese (*toldo* = awning, *segredos* = secrets).

---

## 1. How it works (overview)

```
 Open-Meteo (forecast)          Cloudflare → nginx (auth_basic) → Node-RED
        ▲                                   ▲
        │ HTTPS, once an hour (xx:02)       │ HTTPS, every 5 s (state ⇄ command/config)
        │                                   │
      ┌─┴───────────────────────────────────┴─┐
      │  ESP32-C6-Zero  (toldo_meteo.ino)     │◄── http://toldo.local (local page)
      └───┬──────────┬──────────┬─────────────┘
          │GP2       │GP3       │GP1
        PC817 A    PC817 B    PC817 C
          │          │          │
        S1 UP      S2 STOP    S3 DOWN     ← buttons of a real Gaviota remote (running on its own battery)
                                          → the remote sends the encrypted RF code to the motor
```

- The motor uses an **encrypted rolling code**, so it can't be imitated over RF (that attempt is documented in `historico/`). The solution is to **"press" the buttons of a real remote** with optocouplers.
- **All the logic runs on the ESP32.** If the server is down, the automation and the local page keep working.
- **The ESP32 is the one that contacts the server**, so no ports need to be opened on the router.

---

## 2. Hardware

| Part | Qty | Notes |
|---|---|---|
| Waveshare **ESP32-C6-Zero** | 1 | with soldered headers, on a breadboard |
| **PC817** optocoupler | 3 | one per button |
| **330 Ω** resistor | 3 | about 6 mA through the opto's LED |
| Gaviota remote already paired with the motor | 1 | **keeps its own CR2032 battery** (isolated from the ESP32, no common GND) |
| Breadboard and wires | — | |

### Wiring

| Remote button | Function | GPIO | Opto |
|---|---|---|---|
| S1 | UP (retract) | **GP2** | A |
| S2 | STOP | **GP3** | B |
| S3 | DOWN (extend) | **GP1** | C |

For each channel:

```
GPIO ──[330 Ω]──► PC817 LED anode
                  LED cathode ──► ESP32 GND
                  collector / emitter ──► the two legs of the remote's button (in parallel with it)
```

**PC817:** pins are numbered by position, with the dot or notch at the top left. 1 = anode, 2 = collector, 3 = cathode, 4 = emitter.
(The comments in `comando_optoacoplador.ino` use the datasheet numbering. It means the same thing: LED on one side, transistor on the other.)

**Remote buttons** (4-leg tactile switches, numbered by position: 1 = top-left, 2 = top-right, 3 = bottom-left, 4 = bottom-right):

- Legs 1↔2 are always connected, and so are 3↔4. Pressing the button connects 1/2 to 3/4.
- Use **leg 1** and **leg 4**.
- Measured in circuit, the closed contact reads about 1.8 kΩ because of the rest of the remote. That's normal.
- **If a button doesn't respond, swap that opto's collector and emitter.**

**Pins to avoid on the ESP32-C6-Zero:**
- GP4, GP5, GP8 (RGB LED), GP9 and GP15 (boot), GP12 and GP13 (USB);
- GP6, which is only a pad on the underside of the board.

### Breadboard positions (the build I used)

Rows 1–30. Columns a–e and f–j, with the centre channel between them. "−" is the GND rail. The ESP32 sits in `c1–c9` / `g1–g9`, with the USB-C facing row 1. The ESP32's GND goes from `a2` to −.

| Channel | Opto (notch up) | Resistor | GPIO wire | Cathode → − | To the button |
|---|---|---|---|---|---|
| A (UP) | `e27 f27 e28 f28` | `a25–a27` | GP2: `a6 → b25` | `a28` | `g27` → leg 1, `g28` → leg 4 |
| B (STOP) | `e14 f14 e15 f15` | `a12–a14` | GP3: `a7 → b12` | `a15` | `g14` → leg 1, `g15` → leg 4 |
| C (DOWN) | `e22 f22 e23 f23` | `a20–a22` | GP1: `a5 → b20` | `a23` | `g22` → leg 1, `g23` → leg 4 |

---

## 3. Awning logic (`esp32/toldo_meteo/toldo_meteo.ino`)

States: **UP = retracted** and **DOWN = extended**. The default state is DOWN.

| Situation | What it does |
|---|---|
| **Wind** average > 25 km/h **or** gusts > 30 km/h | **Partial UP**: UP and, 23.5 s later, STOP, so a little stays out. **Wind always takes priority.** |
| Wind continues | doesn't repeat the UP (repeating would retract it fully) |
| **Wind stops** | returns to the state it was in before the wind |
| **Rain** ≥ 0.2 mm/h, no wind | DOWN (extends, to protect the balcony) |
| Rain stops | stays as it is |

**Forecast:**
- Comes from Open-Meteo, no API key needed. It's fetched **at xx:02** (at xx:00 sharp the service often fails from too many requests).
- Uses the maximum of the current hour and the next one.
- If the fetch fails, the awning doesn't move and it retries every 2 min. The page shows why it failed.

**Movement limit:**
- At most **1 movement every 60 min**, counting both automatic moves and button presses.
- If a rule asks for a move before then, the action waits for the next check.
- **Exception:** retracting for wind ignores the limit (configurable).

**Night mode** (by default **20:00 to 07:00**, configurable). At night only two moves are allowed:
1. extended + wind → retract;
2. retracted + rain without wind → extend.

Everything else is postponed until the night ends. For example, if the wind dies down in the early hours, the awning only extends again in the morning. The page buttons always work. If start and end are the same hour, night mode is off.

**Page buttons:**
- Each button is a single click on the remote.
- UP from the pages retracts fully, with no automatic STOP.
- STOP counts as DOWN (partially extended).
- A click during the 23.5 s wait cancels the automatic STOP.

**Memory** (NVS, namespace `toldo`): state, state before the wind, wind mode, time of the last movement and settings (`c_*` keys). Everything survives power cuts. On first boot, with nothing saved, the first check always sends the command, to sync with the real position.

**Serial Monitor** (115200): `u` up, `d` down, `v` check now, `e` show state.

### Settings (no need to re-upload the code)

At `http://toldo.local/config` or `https://toldo.exemplo.com/config`:

| Field | Default | Range |
|---|---|---|
| Average wind (km/h) | 25 | 5–100 |
| Gusts (km/h) | 30 | 5–150 |
| Rain (mm/h) | 0.2 | 0–20 |
| Minutes between movements | 60 | 0–360 (0 = no limit) |
| Wind ignores the limit | yes | — |
| Seconds until STOP | 23.5 | 0–120 (0 = retract fully) |
| Button press duration (ms) | 100 | 50–2000 |
| Night mode: start / end (hour) | 20 / 7 | 0–23 (equal = off) |
| Latitude / longitude | Lisbon airport | — |

The ESP32 validates the values and ignores anything out of range. There's a "Repor valores por defeito" (reset to defaults) button.

---

## 4. Server (public page)

A Linux server running **nginx** and **Node-RED**, behind **Cloudflare**:

1. **Cloudflare:** a `toldo` DNS record with the proxy on and SSL in "Full" mode. If Cloudflare blocks the ESP32 (error 403), add a WAF rule that lets the `/esp` path through.
2. **nginx:** a file in `/etc/nginx/sites-enabled/`, for example:

   ```nginx
   server {
       listen 443 ssl;
       server_name toldo.exemplo.com;
       # ssl_certificate / ssl_certificate_key: your server's
       auth_basic "Toldo";
       auth_basic_user_file /etc/nginx/.htpasswd_toldo;
       location / {
           proxy_pass http://127.0.0.1:1880/toldo/;
           proxy_set_header Host $host;
       }
   }
   ```

3. **Users** (without `-c`, so existing users aren't wiped). It's best to have one just for the ESP32 and another for you:

   ```bash
   sudo htpasswd /etc/nginx/.htpasswd_toldo YOUR_ESP32_USER
   sudo htpasswd /etc/nginx/.htpasswd_toldo YOUR_USER
   sudo cut -d: -f1 /etc/nginx/.htpasswd_toldo   # lists the users (no passwords)
   ```

4. **Node-RED** on port 1880, without exposing that port to the internet. Import `servidor/toldo_nodered_flow.json`. You get a "Toldo" tab with:

| Endpoint | What it does |
|---|---|
| `GET /toldo/` | page with the state, the last 6 h of history and the buttons |
| `POST /toldo/cmd` | button: stores the pending command (expires after 60 s) |
| `POST /toldo/esp` | the ESP32 sends its state and receives `{"cmd":"…","cfg":{…}}` |
| `GET /toldo/config` | settings page |
| `POST /toldo/config` | stores the pending settings change (expires after 10 min) |

### Updating the flow through the Node-RED API (over SSH on the server)

Edit the `fn_*.js` files and run `python3 gerar_fluxo.py`, which generates `toldo_nodered_flow.json`. Then copy that file to the server and run:

```bash
read -p "Node-RED user: " NRU; read -s -p "Node-RED password: " NRP; echo
TOKEN=$(curl -s -X POST http://localhost:1880/auth/token --data-urlencode client_id=node-red-admin --data-urlencode grant_type=password --data-urlencode "scope=*" --data-urlencode "username=$NRU" --data-urlencode "password=$NRP" | python3 -c "import sys,json;print(json.load(sys.stdin)['access_token'])")
FID=$(curl -s http://localhost:1880/flows -H "Authorization: Bearer $TOKEN" | python3 -c "import sys,json;print([x['id'] for x in json.load(sys.stdin) if x.get('type')=='tab' and x.get('label')=='Toldo'][0])")
python3 -c "
import json,sys
n=json.load(open('toldo_nodered_flow.json'))
t=[x for x in n if x['type']=='tab'][0]
json.dump({'id':sys.argv[1],'label':t['label'],'nodes':[x for x in n if x['type']!='tab'],'configs':[]},open('toldo_api.json','w'))
" "$FID"
curl -s -X PUT http://localhost:1880/flow/$FID -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" --data @toldo_api.json; echo
```

The expected response is `{"id":"<tab id>"}`. The command replaces only the "Toldo" tab and leaves your other flows alone. For the first install, import the JSON from the Node-RED editor menu (Import).

---

## 5. Uploading the code to the ESP32

1. Arduino IDE with the **esp32 3.3.x** core. Board **ESP32C6 Dev Module**, with **USB CDC On Boot: Enabled**.
2. **ArduinoJson 7** library, from the Library Manager.
3. Open `esp32/toldo_meteo/toldo_meteo.ino`. The folder must have the same name as the file.
4. Create `segredos.h` from `segredos.exemplo.h` (see section 6) and upload.
5. To test just the optos, use `esp32/comando_optoacoplador/comando_optoacoplador.ino`. In the Serial Monitor: `u`, `s`, `d`, and `p<ms>` to change the press duration.

---

## 6. What you need to fill in (not in the repository)

Copy `esp32/toldo_meteo/segredos.exemplo.h` to `esp32/toldo_meteo/segredos.h` and fill it in. `segredos.h` is in `.gitignore` and is never published. If it's missing, compilation stops with a message explaining what to do.

| Where | Field |
|---|---|
| `segredos.h` | `WIFI_SSID`, `WIFI_PASS` (2.4 GHz network) |
| `segredos.h` | `SERVIDOR_USER`, `SERVIDOR_PASS`: **must exist in `.htpasswd_toldo`** |
| `segredos.h` | `SERVIDOR_URL`: your server's address |
| server | users in `.htpasswd_toldo` and in the Node-RED editor |

---

## 7. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Public page shows "ESP32 sem contacto desde…" (no contact since…), but `toldo.local` works | the ESP32's user or password isn't in `.htpasswd_toldo` (401) | create the user on the server with the password from `segredos.h` (`sudo htpasswd …`, without `-c`) |
| Serial Monitor: `Servidor: HTTP 401` | same as above | same as above |
| `Servidor: HTTP 403` | Cloudflare is blocking the ESP32 | WAF rule for `/esp` |
| `Servidor: HTTP 404` / `502` | nginx or Node-RED | on the server, `curl -s -o /dev/null -w "%{http_code}\n" http://localhost:1880/toldo/` should return 200 |
| Forecast fetch failures | Open-Meteo is slow or rejecting requests | it already fetches at xx:02 and retries every 2 min; check the reason on the page |
| A button doesn't respond | opto transistor polarity | swap the collector and emitter |
| The awning is noisy at night | — | adjust night mode in `/config` |

Testing the server from your PC (curl asks for the password):

```bash
curl -u YOUR_USER -X POST -H "Content-Type: application/json" -d '{"estado":"teste","hist":[]}' https://toldo.exemplo.com/esp
# expected response: {"cmd":""}
```

---

## 8. Files

| File | What it is |
|---|---|
| `esp32/toldo_meteo/toldo_meteo.ino` | main ESP32 code |
| `esp32/toldo_meteo/segredos.exemplo.h` | credentials template (copy to `segredos.h`) |
| `esp32/comando_optoacoplador/comando_optoacoplador.ino` | simple sketch to test the 3 optos |
| `servidor/toldo_nodered_flow.json` | Node-RED flow ("Toldo" tab), ready to import |
| `servidor/fn_pagina.js` | public page: state, history and buttons |
| `servidor/fn_cmd.js` | stores the button that was pressed |
| `servidor/fn_esp.js` | receives the ESP32's state and returns the pending command and settings |
| `servidor/fn_config_pagina.js` | settings page |
| `servidor/fn_config_guardar.js` | stores the settings change |
| `servidor/gerar_fluxo.py` | bundles the `fn_*.js` files into `toldo_nodered_flow.json` |
| `historico/dd27xx-gaviota-resumo.md` | RF phase with the CC1101 (abandoned: encrypted rolling code) |
