# Toldo automático (Gaviota Elite + ESP32)

Controla o toldo da varanda (motor **Gaviota Elite, ref. 60090165**) a partir de um ESP32:

- **automação pela previsão do tempo**: recolhe com vento, estende com chuva;
- **modo noite**: de noite só mexe quando é mesmo preciso, para não fazer barulho;
- **página local** (`http://toldo.local`) e **página pública** (`https://toldo.exemplo.com`), com o estado, o histórico, botões UP / STOP / DOWN e configurações.

> Este repositório **não tem passwords, utilizadores nem chaves**. As credenciais ficam em `esp32/toldo_meteo/segredos.h`, que está no `.gitignore` (ver a secção 6). O domínio `toldo.exemplo.com` é um exemplo: usa o teu.

---

## 1. Como funciona (resumo)

```
 Open-Meteo (previsão)          Cloudflare → nginx (auth_basic) → Node-RED
        ▲                                   ▲
        │ HTTPS, 1x por hora (xx:02)        │ HTTPS, de 5 em 5 s (estado ⇄ comando/config)
        │                                   │
      ┌─┴───────────────────────────────────┴─┐
      │  ESP32-C6-Zero  (toldo_meteo.ino)     │◄── http://toldo.local (página local)
      └───┬──────────┬──────────┬─────────────┘
          │GP2       │GP3       │GP1
        PC817 A    PC817 B    PC817 C
          │          │          │
        S1 SUBIR   S2 STOP    S3 DESCER   ← botões de um comando Gaviota real (com a pilha dele)
                                          → o comando envia o código RF cifrado ao motor
```

- O motor usa **rolling code cifrado**, por isso não dá para o imitar por RF (essa tentativa está em `historico/`). A solução é **"carregar" nos botões de um comando verdadeiro** com opto-acopladores.
- **Toda a lógica corre no ESP32.** Se o servidor estiver em baixo, a automação e a página local continuam a funcionar.
- **Quem contacta o servidor é o ESP32.** Por isso não é preciso abrir portas no router.

---

## 2. Hardware

| Peça | Qtd. | Notas |
|---|---|---|
| Waveshare **ESP32-C6-Zero** | 1 | com pinos soldados, numa breadboard |
| Opto-acoplador **PC817** | 3 | um por botão |
| Resistência **330 Ω** | 3 | cerca de 6 mA no LED do opto |
| Comando Gaviota já emparelhado com o motor | 1 | **fica com a pilha CR2032 dele** (isolado do ESP32, sem GND comum) |
| Breadboard e fios | — | |

### Ligações

| Botão do comando | Função | GPIO | Opto |
|---|---|---|---|
| S1 | SUBIR (UP = recolher) | **GP2** | A |
| S2 | STOP | **GP3** | B |
| S3 | DESCER (DOWN = estender) | **GP1** | C |

Por cada canal:

```
GPIO ──[330 Ω]──► ânodo do LED do PC817
                  cátodo do LED ──► GND do ESP32
                  coletor / emissor ──► as duas patas do botão do comando (em paralelo com ele)
```

**PC817:** a numeração é por posição, com o ponto ou entalhe em cima à esquerda. 1 = ânodo, 2 = coletor, 3 = cátodo, 4 = emissor.
(Nos comentários do `comando_optoacoplador.ino` usa-se a numeração do datasheet. O significado é o mesmo: LED de um lado, transistor do outro.)

**Botões do comando** (tácteis de 4 patas, numeração por posição: 1 = cima-esq, 2 = cima-dta, 3 = baixo-esq, 4 = baixo-dta):

- As patas 1↔2 estão sempre ligadas, e as 3↔4 também. Carregar no botão liga 1/2 a 3/4.
- Usa-se a **pata 1** e a **pata 4**.
- Medido no circuito, o contacto fechado dá cerca de 1,8 kΩ, por causa do resto do comando. É normal.
- **Se um botão não reagir, troca o coletor e o emissor desse opto.**

**Pinos a evitar no ESP32-C6-Zero:**
- GP4, GP5, GP8 (LED RGB), GP9 e GP15 (arranque), GP12 e GP13 (USB);
- GP6, que só existe como pad por baixo da placa.

### Posições na breadboard (montagem usada)

Filas 1–30. Colunas a–e e f–j, com o sulco central entre elas. "−" é a coluna de GND. O ESP32 fica em `c1–c9` / `g1–g9`, com o USB-C virado para a fila 1. O GND do ESP32 vai de `a2` para −.

| Canal | Opto (entalhe em cima) | Resistência | Fio do GPIO | Cátodo → − | Ao botão |
|---|---|---|---|---|---|
| A (SUBIR) | `e27 f27 e28 f28` | `a25–a27` | GP2: `a6 → b25` | `a28` | `g27` → pata 1, `g28` → pata 4 |
| B (STOP) | `e14 f14 e15 f15` | `a12–a14` | GP3: `a7 → b12` | `a15` | `g14` → pata 1, `g15` → pata 4 |
| C (DESCER) | `e22 f22 e23 f23` | `a20–a22` | GP1: `a5 → b20` | `a23` | `g22` → pata 1, `g23` → pata 4 |

---

## 3. Lógica do toldo (`esp32/toldo_meteo/toldo_meteo.ino`)

Estados: **UP = recolhido** e **DOWN = estendido**. O estado por defeito é DOWN.

| Situação | O que faz |
|---|---|
| **Vento** médio > 25 km/h **ou** rajadas > 30 km/h | **UP parcial**: UP e, 23,5 s depois, STOP, para ficar um bocado de fora. **O vento tem sempre prioridade.** |
| O vento continua | não repete o UP (repetir recolheria tudo) |
| **Fim do vento** | volta ao estado que tinha antes do vento |
| **Chuva** ≥ 0,2 mm/h, sem vento | DOWN (estende, para proteger a varanda) |
| Fim da chuva | fica como está |

**Previsão:**
- Vem do Open-Meteo, sem chave de API. É lida **às xx:02** (às xx:00 em ponto o serviço falha muitas vezes por excesso de pedidos).
- Usa o máximo da hora atual e da seguinte.
- Se falhar, não mexe e tenta outra vez de 2 em 2 min. A página mostra o motivo da falha.

**Limite de movimentos:**
- No máximo **1 movimento a cada 60 min**. Contam os automáticos e os botões.
- Se a regra pedir antes disso, a ação fica para a verificação seguinte.
- **Exceção:** recolher por vento ignora o limite (configurável).

**Modo noite** (por defeito **das 20h às 7h**, configurável). De noite só há dois movimentos possíveis:
1. estendido + vento → recolhe;
2. recolhido + chuva sem vento → estende.

Tudo o resto fica adiado para depois do fim da noite. Por exemplo, se o vento parar de madrugada, o toldo só volta a estender de manhã. Os botões das páginas funcionam sempre. Se o início for igual ao fim, o modo noite fica desligado.

**Botões das páginas:**
- Cada botão é um clique simples no comando.
- UP pelas páginas recolhe tudo, sem STOP automático.
- STOP conta como DOWN (parcialmente estendido).
- Um clique durante os 23,5 s de espera cancela o STOP automático.

**Memória** (NVS, namespace `toldo`): estado, estado antes do vento, modo vento, hora do último movimento e configurações (chaves `c_*`). Tudo sobrevive a falhas de luz. No primeiro arranque, sem nada guardado, a primeira verificação envia sempre o comando, para acertar a posição real.

**Monitor Série** (115200): `u` sobe, `d` desce, `v` verifica já, `e` mostra o estado.

### Configurações (sem voltar a carregar o código)

Em `http://toldo.local/config` ou `https://toldo.exemplo.com/config`:

| Campo | Defeito | Limites |
|---|---|---|
| Vento médio (km/h) | 25 | 5–100 |
| Rajadas (km/h) | 30 | 5–150 |
| Chuva (mm/h) | 0,2 | 0–20 |
| Minutos entre movimentos | 60 | 0–360 (0 = sem limite) |
| Vento ignora o limite | sim | — |
| Segundos até ao STOP | 23,5 | 0–120 (0 = recolhe tudo) |
| Duração do toque (ms) | 100 | 50–2000 |
| Modo noite: início / fim (hora) | 20 / 7 | 0–23 (iguais = desligado) |
| Latitude / longitude | aeroporto de Lisboa | — |

O ESP32 valida os valores e ignora os que estiverem fora dos limites. Há um botão "Repor valores por defeito".

---

## 4. Servidor (página pública)

Servidor Linux com **nginx** e **Node-RED**, atrás da **Cloudflare**:

1. **Cloudflare:** um registo DNS `toldo` com proxy ligado e SSL em modo "Full". Se a Cloudflare bloquear o ESP32 (erro 403), cria-se uma regra WAF para deixar passar o caminho `/esp`.
2. **nginx:** um ficheiro em `/etc/nginx/sites-enabled/`, por exemplo:

   ```nginx
   server {
       listen 443 ssl;
       server_name toldo.exemplo.com;
       # ssl_certificate / ssl_certificate_key: os do servidor
       auth_basic "Toldo";
       auth_basic_user_file /etc/nginx/.htpasswd_toldo;
       location / {
           proxy_pass http://127.0.0.1:1880/toldo/;
           proxy_set_header Host $host;
       }
   }
   ```

3. **Utilizadores** (sem `-c`, para não apagar os que já existem). Convém ter um só para o ESP32 e outro para ti:

   ```bash
   sudo htpasswd /etc/nginx/.htpasswd_toldo O_TEU_UTILIZADOR_ESP32
   sudo htpasswd /etc/nginx/.htpasswd_toldo O_TEU_UTILIZADOR
   sudo cut -d: -f1 /etc/nginx/.htpasswd_toldo   # lista os utilizadores (sem passwords)
   ```

4. **Node-RED**, na porta 1880, sem abrir essa porta à internet. Importa `servidor/toldo_nodered_flow.json`. Fica um separador "Toldo" com:

| Endpoint | O que faz |
|---|---|
| `GET /toldo/` | página com o estado, o histórico de 6 h e os botões |
| `POST /toldo/cmd` | botão: guarda o comando pendente (expira ao fim de 60 s) |
| `POST /toldo/esp` | o ESP32 envia o estado e recebe `{"cmd":"…","cfg":{…}}` |
| `GET /toldo/config` | página de configurações |
| `POST /toldo/config` | guarda a alteração pendente (expira ao fim de 10 min) |

### Atualizar o fluxo pela API do Node-RED (no SSH do servidor)

Edita os `fn_*.js` e corre `python3 gerar_fluxo.py`, que gera o `toldo_nodered_flow.json`. Depois envia esse ficheiro para o servidor e corre:

```bash
read -p "Utilizador Node-RED: " NRU; read -s -p "Password Node-RED: " NRP; echo
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

A resposta certa é `{"id":"<id do separador>"}`. O comando substitui só o separador "Toldo" e não mexe nos outros fluxos. Na primeira instalação, importa o JSON pelo menu do editor do Node-RED (Import).

---

## 5. Carregar o código no ESP32

1. Arduino IDE com o core **esp32 3.3.x**. Placa **ESP32C6 Dev Module**, com **USB CDC On Boot: Enabled**.
2. Biblioteca **ArduinoJson 7**, pelo Gestor de Bibliotecas.
3. Abre `esp32/toldo_meteo/toldo_meteo.ino`. A pasta tem de ter o mesmo nome do ficheiro.
4. Cria o `segredos.h` a partir do `segredos.exemplo.h` (ver a secção 6) e carrega.
5. Para testar só os optos, há o `esp32/comando_optoacoplador/comando_optoacoplador.ino`. No Monitor Série: `u`, `s`, `d`, e `p<ms>` para mudar a duração do toque.

---

## 6. O que tens de preencher (não vem no repositório)

Copia `esp32/toldo_meteo/segredos.exemplo.h` para `esp32/toldo_meteo/segredos.h` e preenche. O `segredos.h` está no `.gitignore` e nunca é publicado.

| Onde | Campo |
|---|---|
| `segredos.h` | `WIFI_SSID`, `WIFI_PASS` (rede de 2,4 GHz) |
| `segredos.h` | `SERVIDOR_USER`, `SERVIDOR_PASS`: **têm de existir no `.htpasswd_toldo`** |
| `segredos.h` | `SERVIDOR_URL`: o endereço do teu servidor |
| servidor | utilizadores do `.htpasswd_toldo` e do editor do Node-RED |

---

## 7. Resolução de problemas

| Sintoma | Causa provável | Solução |
|---|---|---|
| Página pública: "ESP32 sem contacto desde…", mas `toldo.local` funciona | o utilizador ou a password do ESP32 não existem no `.htpasswd_toldo` (401) | criar o utilizador no servidor com a password que está no `segredos.h` (`sudo htpasswd …`, sem `-c`) |
| Monitor Série: `Servidor: HTTP 401` | o mesmo | o mesmo |
| `Servidor: HTTP 403` | a Cloudflare bloqueia o ESP32 | regra WAF para `/esp` |
| `Servidor: HTTP 404` / `502` | nginx ou Node-RED | `curl -s -o /dev/null -w "%{http_code}\n" http://localhost:1880/toldo/` no servidor deve dar 200 |
| Falhas a obter a previsão | o Open-Meteo está lento ou a recusar pedidos | já lê às xx:02 e tenta de 2 em 2 min; ver o motivo na página |
| Um botão não reage | polaridade do transistor do opto | trocar o coletor e o emissor |
| O toldo faz barulho de noite | — | ajustar o modo noite em `/config` |

Teste ao servidor a partir do PC (o curl pede a password):

```bash
curl -u O_TEU_UTILIZADOR -X POST -H "Content-Type: application/json" -d '{"estado":"teste","hist":[]}' https://toldo.exemplo.com/esp
# resposta certa: {"cmd":""}
```

---

## 8. Ficheiros

| Ficheiro | O que é |
|---|---|
| `esp32/toldo_meteo/toldo_meteo.ino` | código principal do ESP32 |
| `esp32/toldo_meteo/segredos.exemplo.h` | modelo das credenciais (copiar para `segredos.h`) |
| `esp32/comando_optoacoplador/comando_optoacoplador.ino` | sketch simples para testar os 3 optos |
| `servidor/toldo_nodered_flow.json` | fluxo do Node-RED (separador "Toldo"), pronto a importar |
| `servidor/fn_pagina.js` | página pública: estado, histórico e botões |
| `servidor/fn_cmd.js` | guarda o botão carregado |
| `servidor/fn_esp.js` | recebe o estado do ESP32 e devolve o comando e a configuração pendentes |
| `servidor/fn_config_pagina.js` | página de configurações |
| `servidor/fn_config_guardar.js` | guarda a alteração de configuração |
| `servidor/gerar_fluxo.py` | junta os `fn_*.js` no `toldo_nodered_flow.json` |
| `historico/dd27xx-gaviota-resumo.md` | fase RF com o CC1101 (abandonada: rolling code cifrado) |
