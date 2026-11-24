/*
  toldo_meteo.ino - toldo automatico com a previsao do tempo + pagina web
  ESP32-C6-Zero + PC817 no comando Gaviota: SUBIR = GP2, STOP = GP3, DESCER = GP1.

  A cada hora certa le a previsao (Open-Meteo) para a hora que comeca:
    - vento medio > 25 km/h OU rajadas > 30 km/h -> UP (recolher). Tem prioridade.
      O recolher e parcial: UP e, 23,5 s depois, STOP (fica um bocado de fora).
    - enquanto o vento continuar, fica UP (nao repete o UP, para nao recolher tudo).
    - quando o vento acaba -> volta ao estado de antes do vento.
    - chuva >= CHUVA_MIN (sem vento forte) -> DOWN (estender, protege a varanda).
    - quando a chuva acaba, o toldo fica como esta.
  A previsao e lida as xx:02 (as xx:00 em ponto o Open-Meteo recebe pedidos de
  meio mundo e falha muitas vezes). A acao e feita logo a seguir.
  Se a previsao falhar, nao mexe e tenta de 2 em 2 minutos; a acao e feita
  quando os dados chegarem. Depois volta ao ritmo de 1x por hora, as xx:02.
  No maximo 1 movimento por hora: depois de o toldo mexer (automatico ou botao),
  a automacao so volta a mexe-lo 60 min depois. Se a regra pedir antes disso,
  a acao fica adiada para a verificacao seguinte. Excecao: recolher por vento
  (VENTO_IGNORA_LIMITE = true), para proteger o toldo.
  Modo noite (por defeito 20h-7h, muda-se em /config): para nao fazer barulho, a
  automacao so mexe para recolher por vento (se estiver estendido) ou para estender com
  chuva sem vento (se estiver recolhido). O resto (ex.: repor depois do vento) fica
  adiado para a primeira verificacao depois do fim da noite. Os botoes funcionam sempre.
  Estado por defeito (primeiro arranque, sem nada guardado): DOWN (estendido).
  Nesse caso, a primeira verificacao envia sempre o comando, para acertar o toldo.

  Os limites (vento, rajadas, chuva, tempo entre movimentos, STOP, toque, noite, local) mudam-se
  em http://toldo.local/config e ficam guardados no ESP32. Nao e preciso voltar a carregar o codigo.

  Pagina web: http://toldo.local  (ou o IP que aparece no Monitor Serie)
  Mostra o estado, a previsao, quando foi buscada e a acao, das ultimas 6 horas.
  Botoes UP / STOP / DOWN: cada um simula um clique no botao do comando (UP recolhe tudo).
  Depois de um STOP na pagina, o ESP32 conta o toldo como DOWN (parcialmente estendido).

  Servidor (https://toldo.exemplo.com): de 5 em 5 s o ESP32 envia o estado
  ao Node-RED e recebe o botao carregado na pagina publica (se houver). A automacao do
  vento e da chuva continua no ESP32 e funciona mesmo sem o servidor.

  Biblioteca necessaria: ArduinoJson (Gestor de Bibliotecas do Arduino IDE).
  Monitor serie 115200 baud:  u = subir   d = descer   v = verificar ja   e = ver estado
*/
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

// ------------------- configura aqui -------------------
// Rede Wi-Fi, endereco do servidor e credenciais ficam em segredos.h (nao vai para o git).
// Copia segredos.exemplo.h para segredos.h e preenche.
#if __has_include("segredos.h")
#include "segredos.h"
#else
#error "Falta o segredos.h: copia segredos.exemplo.h para segredos.h e preenche (ver README, secao 6)."
#endif
const char* NOME_HOST = "toldo";             // -> http://toldo.local
const unsigned long SYNC_MS = 5000;                   // contacto com o servidor a cada 5 s

#define PIN_UP    2
#define PIN_STOP  3
#define PIN_DOWN  1

// Valores por defeito. Mudam-se em http://toldo.local/config (ficam guardados no ESP32).
const float DEF_LAT = 38.77, DEF_LON = -9.13;   // junto ao aeroporto de Lisboa
const float DEF_VENTO  = 25.0;   // km/h, vento medio (recolhe se ACIMA disto)
const float DEF_RAJADA = 30.0;   // km/h, rajadas     (recolhe se ACIMA disto)
const float DEF_CHUVA  = 0.2;    // mm na hora
const long  DEF_INTERVALO_S = 3600;            // no maximo 1 movimento automatico por hora
const bool  DEF_VENTO_IGNORA = true;           // recolher por vento pode ignorar esse limite
const int   DEF_PULSO_MS = 100;                // duracao do toque nos botoes
const unsigned long DEF_STOP_MS = 23500;       // UP parcial: STOP 23,5 s depois (0 = recolhe tudo)
const int   DEF_NOITE_INI = 20;                // modo noite a partir das 20:00 ...
const int   DEF_NOITE_FIM = 7;                 // ... ate as 07:00: so vento ou chuva mexem (igual = desligado)
// ------------------------------------------------------

// valores em uso (carregados da memoria no arranque)
float LAT = DEF_LAT, LON = DEF_LON;
float VENTO_MAX = DEF_VENTO, RAJADA_MAX = DEF_RAJADA, CHUVA_MIN = DEF_CHUVA;
long  INTERVALO_MIN_S = DEF_INTERVALO_S;
bool  VENTO_IGNORA_LIMITE = DEF_VENTO_IGNORA;
int   PULSO_MS = DEF_PULSO_MS;
unsigned long STOP_APOS_MS = DEF_STOP_MS;
int   NOITE_INICIO = DEF_NOITE_INI, NOITE_FIM = DEF_NOITE_FIM;

enum Estado : uint8_t { UP = 0, DOWN = 1 };

Preferences prefs;
WebServer server(80);

Estado estado;       // o que o ESP32 pensa que o toldo esta
Estado antesVento;   // estado guardado quando o vento comecou
bool   emVento;      // estamos em modo vento?
bool   desconhecido; // primeiro arranque: nao sabemos onde esta o toldo

const int MINUTO_VERIFICAR = 2;                     // le a previsao as xx:02 (nao em ponto)
const unsigned long REPETIR_MS = 2UL * 60 * 1000;    // falhou: tenta de 2 em 2 min
int ultimaHora = -1;               // ultima hora ja verificada com sucesso
unsigned long ultimaTentativa = 0; // millis() da ultima tentativa falhada (0 = nenhuma)
int horaTentativa = -1;            // hora da ultima tentativa falhada
time_t ultimaFalha = 0;            // hora da ultima falha (para a pagina)
String erroPrevisao;               // motivo da ultima falha (para a pagina)

// ---- historico (em memoria; perde-se se o ESP32 reiniciar) ----
struct Registo { time_t quando; float vento, rajada, chuva; String acao; };

// Valores de configuracao vindos de uma pagina (local ou do servidor), ainda em texto.
struct CfgTexto { String vento, rajada, chuva, interv, stop, pulso, lat, lon, nini, nfim; bool vign; };
bool aplicarConfig(const CfgTexto& c);   // definidas mais abaixo
void reporConfig();
const int NREG = 12;
Registo regs[NREG];
int nRegs = 0, proxReg = 0;

void registar(float v, float r, float c, const String& acao){
  regs[proxReg] = { time(nullptr), v, r, c, acao };
  proxReg = (proxReg + 1) % NREG;
  if(nRegs < NREG) nRegs++;
}

const char* nome(Estado e){ return e == UP ? "UP (recolhido parcial)" : "DOWN (estendido)"; }

String hora(time_t t, const char* fmt = "%d/%m %H:%M"){
  struct tm tm; localtime_r(&t, &tm);
  char b[24]; strftime(b, sizeof b, fmt, &tm);
  return String(b);
}

bool temHora(){ return time(nullptr) > 1700000000; }   // NTP ja sincronizou?

time_t ultimoMov = 0;                    // hora do ultimo movimento (guardada; sobrevive a reinicios)

void guardar(){
  prefs.putUChar("estado", estado);
  prefs.putUChar("antes", antesVento);
  prefs.putBool("vento", emVento);
  prefs.putUInt("ultMov", (uint32_t)ultimoMov);
}

bool deNoite(){                          // entre NOITE_INICIO e NOITE_FIM? (iguais = modo noite desligado)
  if(!temHora() || NOITE_INICIO == NOITE_FIM) return false;
  time_t agora = time(nullptr);
  struct tm t; localtime_r(&agora, &t);
  int h = t.tm_hour;
  if(NOITE_INICIO < NOITE_FIM) return h >= NOITE_INICIO && h < NOITE_FIM;   // ex.: 1h-6h
  return h >= NOITE_INICIO || h < NOITE_FIM;                                // ex.: 20h-7h (passa a meia-noite)
}

bool podeMover(){                        // ja passou 1 h desde o ultimo movimento?
  return ultimoMov == 0 || !temHora() || time(nullptr) - ultimoMov >= INTERVALO_MIN_S;
}

void pulso(int pin){
  digitalWrite(pin, HIGH);   // opto conduz = carregar no botao
  delay(PULSO_MS);
  digitalWrite(pin, LOW);
}

bool cancelar = false;                   // um botao da pagina cancela o STOP automatico

void sincronizar();                      // definida mais abaixo
unsigned long ultimoSync = 0;

void servir(){                           // pagina local + contacto com o servidor
  server.handleClient();
  if(millis() - ultimoSync >= SYNC_MS){ ultimoSync = millis(); sincronizar(); }
}

void esperar(unsigned long ms){          // espera sem bloquear as paginas
  unsigned long t0 = millis();
  while(millis() - t0 < ms && !cancelar){ servir(); delay(5); }
}

void mover(Estado e){
  ultimoMov = time(nullptr);
  if(e == UP){
    cancelar = false;
    pulso(PIN_UP);
    if(STOP_APOS_MS > 0){                // 0 = recolhe tudo, sem STOP
      Serial.printf("UP enviado; STOP daqui a %.1f s...\n", STOP_APOS_MS / 1000.0);
      esperar(STOP_APOS_MS);
      if(cancelar){ Serial.println("STOP automatico cancelado pela pagina."); return; }
      pulso(PIN_STOP);                   // para antes de recolher tudo
    }
  } else {
    pulso(PIN_DOWN);
  }
  estado = e;
  desconhecido = false;
  guardar();
  Serial.printf("-> %s\n", nome(e));
}

// Le vento, rajadas e chuva maximos da hora atual e da seguinte.
bool lerPrevisao(float &vento, float &rajada, float &chuva){
  if(!temHora()) return false;
  time_t agora = time(nullptr);
  struct tm t; localtime_r(&agora, &t);
  int h0 = t.tm_hour, h1 = (h0 + 1) % 24;

  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(LAT, 4) +
               "&longitude=" + String(LON, 4) +
               "&hourly=wind_speed_10m,wind_gusts_10m,precipitation"
               "&forecast_hours=3&timezone=Europe%2FLisbon";

  WiFiClientSecure cli;
  cli.setInsecure();           // nao valida o certificado (dados publicos de meteo)
  HTTPClient http;
  if(!http.begin(cli, url)){ erroPrevisao = "ligação"; return false; }
  http.setTimeout(15000);      // o Open-Meteo pode demorar uns segundos
  int code = http.GET();
  if(code != 200){
    erroPrevisao = code < 0 ? HTTPClient::errorToString(code) : String("HTTP ") + code;
    Serial.printf("Erro HTTP %d (%s)\n", code, erroPrevisao.c_str());
    http.end(); return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if(err){ erroPrevisao = String("JSON: ") + err.c_str(); Serial.printf("Erro JSON: %s\n", err.c_str()); return false; }

  JsonArray horas = doc["hourly"]["time"];       // ex.: "2026-10-15T13:00"
  vento = rajada = chuva = 0;
  int usados = 0;
  for(size_t i = 0; i < horas.size(); i++){
    const char* s = horas[i];
    if(!s || strlen(s) < 13) continue;
    int h = atoi(s + 11);                        // hora do texto "AAAA-MM-DDTHH:MM"
    if(h != h0 && h != h1) continue;             // so a hora atual e a seguinte
    vento  = max(vento,  doc["hourly"]["wind_speed_10m"][i].as<float>());
    rajada = max(rajada, doc["hourly"]["wind_gusts_10m"][i].as<float>());
    chuva  = max(chuva,  doc["hourly"]["precipitation"][i].as<float>());
    usados++;
  }
  if(!usados) erroPrevisao = "sem dados para esta hora";
  return usados > 0;
}

// Proxima verificacao normal: as xx:02 desta hora (se ainda nao foi feita) ou da seguinte.
String proximaVerificacao(time_t agora){
  struct tm t; localtime_r(&agora, &t);
  int h = (t.tm_hour != ultimaHora) ? t.tm_hour : (t.tm_hour + 1) % 24;
  char b[8]; snprintf(b, sizeof b, "%02d:%02d", h, MINUTO_VERIFICAR);
  return String(b);
}

bool verificar(){
  float vento, rajada, chuva;
  if(!lerPrevisao(vento, rajada, chuva)){
    Serial.println("Falhou a previsao. Tento outra vez daqui a 2 min.");
    ultimaFalha = time(nullptr);
    return false;
  }
  Serial.printf("Previsao: vento %.0f km/h, rajadas %.0f km/h, chuva %.1f mm\n", vento, rajada, chuva);

  bool ventoForte = (vento > VENTO_MAX) || (rajada > RAJADA_MAX);
  bool chove = (chuva >= CHUVA_MIN);
  String adiado = String(": adiado (mexeu às ") + hora(ultimoMov, "%H:%M") + ")";
  String acao;

  if(ventoForte){
    bool novo = !emVento;
    if(novo){ antesVento = estado; emVento = true; }     // memoriza o estado antes do vento
    if(estado != UP || desconhecido){                    // vento tem prioridade
      if(VENTO_IGNORA_LIMITE || desconhecido || podeMover()){
        mover(UP);
        acao = "Vento forte: UP parcial";
      } else {
        guardar();
        acao = "Vento forte" + adiado;
      }
    } else {
      guardar();                                         // ja esta UP: nao repete (recolheria tudo)
      acao = novo ? "Vento forte: já estava UP" : "Vento continua: fica UP";
    }
  } else {
    bool acabouVento = emVento;
    Estado alvo = estado;
    if(emVento) alvo = antesVento;                       // vento acabou: repor
    if(chove) alvo = DOWN;                               // chuva: estender

    if(acabouVento && chove) acao = "Fim do vento + chuva";
    else if(acabouVento)     acao = "Fim do vento (repor)";
    else if(chove)           acao = "Chuva";
    else                     acao = "Sem vento nem chuva";

    // De noite so a chuva (sem vento) pode estender um toldo recolhido.
    bool noiteBloqueia = deNoite() && !desconhecido && !(chove && alvo == DOWN && estado == UP);

    if((alvo != estado || desconhecido) && noiteBloqueia){
      guardar();                                         // mantem o modo vento: repoe depois das 7h
      acao += String(": noite, adiado para depois das ") + (NOITE_FIM < 10 ? "0" : "") + String(NOITE_FIM) + ":00";
    } else if(alvo != estado || desconhecido){
      if(desconhecido || podeMover()){
        mover(alvo);
        emVento = false;
        guardar();
        acao += String(": ") + (alvo == UP ? "UP" : "DOWN");
      } else {
        guardar();                                       // fica em modo vento ate conseguir repor
        acao += adiado;
      }
    } else {
      emVento = false;
      guardar();
      acao += ": sem mudança";
    }
  }
  registar(vento, rajada, chuva, acao);
  return true;
}

// ---------------- pagina web ----------------
String num(float v, int casas){ return isnan(v) ? String("-") : String(v, casas); }

// Um clique simples no botao do comando (pagina local ou pagina do servidor).
void clique(int pin, Estado novo, const String& txt){
  cancelar = true;                       // se havia um UP parcial a decorrer, nao manda o STOP
  pulso(pin);
  ultimoMov = time(nullptr);             // um clique tambem conta para o limite de 1 h
  estado = novo;
  desconhecido = false;
  guardar();
  registar(NAN, NAN, NAN, txt);
  Serial.println(txt);
}

void botao(int pin, Estado novo, const char* txt){
  clique(pin, novo, String("Página: ") + txt);
  server.sendHeader("Location", "/");
  server.send(303);
}

// Envia o estado ao servidor e executa o botao carregado na pagina publica (se houver).
bool aSincronizar = false;
void sincronizar(){
  if(aSincronizar || WiFi.status() != WL_CONNECTED || !temHora()) return;
  aSincronizar = true;

  time_t agora = time(nullptr);
  JsonDocument d;
  d["estado"] = nome(estado);
  d["emVento"] = emVento;
  d["antesVento"] = nome(antesVento);
  d["desconhecido"] = desconhecido;
  d["falhaPrevisao"] = (ultimaTentativa != 0);
  if(ultimaTentativa){
    unsigned long passou = millis() - ultimaTentativa;
    d["proxima"] = hora(agora + (passou < REPETIR_MS ? (REPETIR_MS - passou) / 1000 : 0), "%H:%M");
  } else {
    d["proxima"] = proximaVerificacao(agora);
  }
  d["ventoMax"] = String(String(VENTO_MAX, 0) + " / " + String(RAJADA_MAX, 0));   // vento / rajadas
  d["chuvaMin"] = CHUVA_MIN;
  JsonObject cfg = d["cfg"].to<JsonObject>();        // configuracao em uso (para a pagina do servidor)
  cfg["vento"]  = VENTO_MAX;
  cfg["rajada"] = RAJADA_MAX;
  cfg["chuva"]  = CHUVA_MIN;
  cfg["interv"] = INTERVALO_MIN_S / 60;
  cfg["vign"]   = VENTO_IGNORA_LIMITE;
  cfg["stop"]   = STOP_APOS_MS / 1000.0f;
  cfg["pulso"]  = PULSO_MS;
  cfg["nini"]   = NOITE_INICIO;
  cfg["nfim"]   = NOITE_FIM;
  cfg["lat"]    = LAT;
  cfg["lon"]    = LON;
  JsonArray hist = d["hist"].to<JsonArray>();
  for(int k = 0; k < nRegs; k++){
    int i = (proxReg - 1 - k + NREG) % NREG;           // do mais recente para o mais antigo
    if(agora - regs[i].quando > 6 * 3600) continue;
    JsonObject o = hist.add<JsonObject>();
    o["t"] = (uint32_t)regs[i].quando;
    if(!isnan(regs[i].vento)){
      o["v"] = (int)lroundf(regs[i].vento);
      o["r"] = (int)lroundf(regs[i].rajada);
      o["c"] = roundf(regs[i].chuva * 10) / 10;
    }
    o["a"] = regs[i].acao;
  }
  String corpo;
  serializeJson(d, corpo);

  WiFiClientSecure cli;
  cli.setInsecure();
  HTTPClient http;
  http.setTimeout(5000);
  String cmd = "";
  JsonDocument r;
  bool temResposta = false;
  if(http.begin(cli, SERVIDOR_URL)){
    http.setAuthorization(SERVIDOR_USER, SERVIDOR_PASS);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(corpo);
    if(code == 200){
      if(!deserializeJson(r, http.getString())){ temResposta = true; cmd = String(r["cmd"] | ""); }
    } else {
      Serial.printf("Servidor: HTTP %d\n", code);
    }
    http.end();
  }
  aSincronizar = false;

  if(cmd == "up")        clique(PIN_UP,   UP,   "Servidor: UP");
  else if(cmd == "down") clique(PIN_DOWN, DOWN, "Servidor: DOWN");
  else if(cmd == "stop") clique(PIN_STOP, DOWN, "Servidor: STOP (conta como DOWN)");

  // configuracoes mudadas na pagina do servidor
  bool mudouCfg = false;
  if(temResposta && r["cfg"].is<JsonObject>()){
    JsonObject rc = r["cfg"];
    if(rc["reset"] | false){
      reporConfig();
      registar(NAN, NAN, NAN, "Configurações (servidor): valores por defeito");
    } else {
      auto txt = [](JsonVariant v) -> String {           // numero ou texto -> texto
        if(v.isNull()) return String("");
        if(v.is<const char*>()) return String(v.as<const char*>());
        return String(v.as<float>(), 4);
      };
      CfgTexto c;
      c.vento = txt(rc["vento"]);   c.rajada = txt(rc["rajada"]); c.chuva = txt(rc["chuva"]);
      c.interv = txt(rc["interv"]); c.stop = txt(rc["stop"]);     c.pulso = txt(rc["pulso"]);
      c.lat = txt(rc["lat"]);       c.lon = txt(rc["lon"]);
      c.nini = txt(rc["nini"]);     c.nfim = txt(rc["nfim"]);
      c.vign = rc["vign"] | VENTO_IGNORA_LIMITE;
      bool ok = aplicarConfig(c);
      registar(NAN, NAN, NAN, ok ? "Configurações alteradas (servidor)"
                                 : "Configurações alteradas (servidor, alguns valores ignorados)");
    }
    mudouCfg = true;
  }
  if(cmd.length() || mudouCfg) ultimoSync = millis() - SYNC_MS + 1000;   // mostra o novo estado ja daqui a 1 s
}

void paginaInicial(){
  time_t agora = time(nullptr);
  String h;
  h.reserve(4000);
  h += "<!doctype html><html lang='pt'><head><meta charset='utf-8'>"
       "<meta name='viewport' content='width=device-width,initial-scale=1'>"
       "<meta http-equiv='refresh' content='60'><title>Toldo</title><style>"
       "body{font-family:system-ui,sans-serif;margin:16px;max-width:640px;color:#222;background:#fafafa}"
       "h1{font-size:22px;margin:0 0 12px}.card{background:#fff;border:1px solid #ddd;border-radius:10px;padding:12px;margin-bottom:12px}"
       "table{width:100%;border-collapse:collapse;font-size:14px}th,td{padding:6px 4px;border-bottom:1px solid #eee;text-align:left}"
       "th{color:#666;font-weight:600}.alerta{color:#b00020;font-weight:600}.muted{color:#777;font-size:13px}"
       ".botoes{display:flex;gap:8px}.botoes form{flex:1;margin:0}"
       "button{width:100%;padding:16px 0;font-size:18px;font-weight:600;border-radius:10px;border:1px solid #bbb;background:#fff;cursor:pointer}"
       "button:active{background:#eee}.stop{border-color:#b00020;color:#b00020}"
       "</style></head><body><h1>Toldo</h1>"
       "<div class='card botoes'>"
       "<form method='post' action='/up'><button>▲ UP</button></form>"
       "<form method='post' action='/stop'><button class='stop'>■ STOP</button></form>"
       "<form method='post' action='/down'><button>▼ DOWN</button></form>"
       "</div><div class='card'>";

  h += "<div><b>Estado:</b> " + String(nome(estado)) + "</div>";
  if(emVento) h += "<div><b>Modo vento:</b> sim (antes estava " + String(nome(antesVento)) + ")</div>";
  else        h += "<div><b>Modo vento:</b> não</div>";
  if(temHora()){
    h += "<div><b>Agora:</b> " + hora(agora) + "</div>";
    if(ultimaTentativa){
      unsigned long passou = millis() - ultimaTentativa;
      time_t prox = agora + (passou < REPETIR_MS ? (REPETIR_MS - passou) / 1000 : 0);
      h += "<div class='alerta'>Previsão em falta. Nova tentativa às " + hora(prox, "%H:%M") + "</div>";
    } else {
      h += "<div><b>Próxima verificação:</b> " + proximaVerificacao(agora) + "</div>";
    }
  } else {
    h += "<div class='alerta'>Ainda sem hora certa (NTP).</div>";
  }
  if(ultimaFalha && agora - ultimaFalha < 6 * 3600)
    h += "<div class='alerta'>Última falha a obter a previsão: " + hora(ultimaFalha) +
         (erroPrevisao.length() ? " (" + erroPrevisao + ")" : "") + "</div>";
  if(ultimoMov) h += "<div><b>Último movimento:</b> " + hora(ultimoMov) + "</div>";
  h += "<div class='muted'>Limites: vento &gt; " + String(VENTO_MAX, 0) + " km/h ou rajadas &gt; " +
       String(RAJADA_MAX, 0) + " km/h → UP. Chuva ≥ " + String(CHUVA_MIN, 1) + " mm → DOWN. ";
  if(INTERVALO_MIN_S > 0)
    h += "No máximo 1 movimento automático a cada " + String(INTERVALO_MIN_S / 60) + " min" +
         (VENTO_IGNORA_LIMITE ? " (exceto recolher por vento)" : "") + ". ";
  if(NOITE_INICIO != NOITE_FIM)
    h += "Das " + String(NOITE_INICIO) + "h às " + String(NOITE_FIM) + "h só mexe por vento ou chuva" +
         (deNoite() ? " (agora)" : "") + ". ";
  h += "<a href='/config'>Configurações</a></div></div>";

  h += "<div class='card'><b>Últimas 6 horas</b><table><tr><th>Buscada</th><th>Vento</th>"
       "<th>Rajadas</th><th>Chuva</th><th>Ação</th></tr>";
  int mostrados = 0;
  for(int k = 0; k < nRegs; k++){
    int i = (proxReg - 1 - k + NREG) % NREG;           // do mais recente para o mais antigo
    if(agora - regs[i].quando > 6 * 3600) continue;
    h += "<tr><td>" + hora(regs[i].quando) + "</td><td>" + num(regs[i].vento, 0) + " km/h</td><td>" +
         num(regs[i].rajada, 0) + " km/h</td><td>" + num(regs[i].chuva, 1) + " mm</td><td>" +
         regs[i].acao + "</td></tr>";
    mostrados++;
  }
  if(!mostrados) h += "<tr><td colspan='5' class='muted'>Sem registos nas últimas 6 horas.</td></tr>";
  h += "</table><div class='muted'>Previsão Open-Meteo. A página atualiza a cada minuto.</div></div></body></html>";

  server.send(200, "text/html; charset=utf-8", h);
}

// ---------------- configuracoes (http://toldo.local/config) ----------------
void carregarConfig(){
  LAT                 = prefs.getFloat("c_lat",    DEF_LAT);
  LON                 = prefs.getFloat("c_lon",    DEF_LON);
  VENTO_MAX           = prefs.getFloat("c_vento",  DEF_VENTO);
  RAJADA_MAX          = prefs.getFloat("c_rajada", DEF_RAJADA);
  CHUVA_MIN           = prefs.getFloat("c_chuva",  DEF_CHUVA);
  INTERVALO_MIN_S     = prefs.getLong ("c_interv", DEF_INTERVALO_S);
  VENTO_IGNORA_LIMITE = prefs.getBool ("c_vign",   DEF_VENTO_IGNORA);
  PULSO_MS            = prefs.getInt  ("c_pulso",  DEF_PULSO_MS);
  STOP_APOS_MS        = prefs.getULong("c_stop",   DEF_STOP_MS);
  NOITE_INICIO        = prefs.getInt  ("c_nini",   DEF_NOITE_INI);
  NOITE_FIM           = prefs.getInt  ("c_nfim",   DEF_NOITE_FIM);
}

void guardarConfig(){
  prefs.putFloat("c_lat",    LAT);
  prefs.putFloat("c_lon",    LON);
  prefs.putFloat("c_vento",  VENTO_MAX);
  prefs.putFloat("c_rajada", RAJADA_MAX);
  prefs.putFloat("c_chuva",  CHUVA_MIN);
  prefs.putLong ("c_interv", INTERVALO_MIN_S);
  prefs.putBool ("c_vign",   VENTO_IGNORA_LIMITE);
  prefs.putInt  ("c_pulso",  PULSO_MS);
  prefs.putULong("c_stop",   STOP_APOS_MS);
  prefs.putInt  ("c_nini",   NOITE_INICIO);
  prefs.putInt  ("c_nfim",   NOITE_FIM);
}

void reporConfig(){
  const char* chaves[] = { "c_lat", "c_lon", "c_vento", "c_rajada", "c_chuva",
                           "c_interv", "c_vign", "c_pulso", "c_stop", "c_nini", "c_nfim" };
  for(const char* k : chaves) prefs.remove(k);
  carregarConfig();
}

String campo(const char* nomeCampo, const char* rotulo, const String& valor, const char* ajuda){
  return String("<label><b>") + rotulo + "</b><input type='number' step='any' name='" + nomeCampo +
         "' value='" + valor + "'></label><div class='muted'>" + ajuda + "</div>";
}

void paginaConfig(){
  String h;
  h.reserve(5000);
  h += "<!doctype html><html lang='pt'><head><meta charset='utf-8'>"
       "<meta name='viewport' content='width=device-width,initial-scale=1'><title>Toldo: configurações</title><style>"
       "body{font-family:system-ui,sans-serif;margin:16px;max-width:640px;color:#222;background:#fafafa}"
       "h1{font-size:22px;margin:0 0 12px}.card{background:#fff;border:1px solid #ddd;border-radius:10px;padding:12px;margin-bottom:12px}"
       "label{display:block;margin-top:12px}input[type=number]{display:block;width:100%;box-sizing:border-box;padding:8px;font-size:16px;margin-top:4px;border:1px solid #bbb;border-radius:8px}"
       ".muted{color:#777;font-size:13px;margin-top:2px}.ok{color:#1b7f3b;font-weight:600}.alerta{color:#b00020;font-weight:600}"
       "button{padding:12px 18px;font-size:16px;font-weight:600;border-radius:10px;border:1px solid #bbb;background:#fff;cursor:pointer;margin-top:16px}"
       ".principal{background:#1b5fb3;color:#fff;border-color:#1b5fb3}"
       "</style></head><body><h1>Configurações</h1>";

  if(server.arg("ok") == "1") h += "<div class='card ok'>Guardado. Aplica-se a partir da próxima verificação.</div>";
  if(server.arg("ok") == "2") h += "<div class='card alerta'>Guardado, mas alguns valores estavam fora dos limites e ficaram como estavam.</div>";

  h += "<form method='post' action='/config' class='card'>";
  h += campo("vento",  "Vento médio (km/h)", String(VENTO_MAX, 1),  "Recolhe se o vento médio previsto for acima deste valor. Entre 5 e 100.");
  h += campo("rajada", "Rajadas (km/h)",     String(RAJADA_MAX, 1), "Recolhe se as rajadas previstas forem acima deste valor. Entre 5 e 150.");
  h += campo("chuva",  "Chuva (mm numa hora)", String(CHUVA_MIN, 1), "Estende se a chuva prevista for igual ou acima deste valor. Entre 0 e 20.");
  h += campo("interv", "Tempo mínimo entre movimentos (min)", String(INTERVALO_MIN_S / 60),
             "A automação não mexe o toldo mais do que uma vez neste intervalo. 0 = sem limite. Entre 0 e 360.");
  h += String("<label><input type='checkbox' name='vign' value='1'") + (VENTO_IGNORA_LIMITE ? " checked" : "") +
       "> <b>O vento pode recolher o toldo mesmo antes desse tempo</b></label>";
  h += campo("stop",   "STOP depois do UP (s)", String(STOP_APOS_MS / 1000.0, 1),
             "No recolher por vento: segundos até ao STOP, para ficar um bocado de fora. 0 = recolhe tudo. Entre 0 e 120.");
  h += campo("pulso",  "Duração do toque nos botões (ms)", String(PULSO_MS), "Entre 50 e 2000.");
  h += campo("nini",   "Modo noite: início (hora)", String(NOITE_INICIO),
             "A partir desta hora só mexe para recolher por vento ou estender com chuva. Entre 0 e 23.");
  h += campo("nfim",   "Modo noite: fim (hora)", String(NOITE_FIM),
             "Até esta hora. Igual ao início = modo noite desligado. Entre 0 e 23.");
  h += campo("lat",    "Latitude",  String(LAT, 4), "Local da previsão. Entre -90 e 90.");
  h += campo("lon",    "Longitude", String(LON, 4), "Local da previsão. Entre -180 e 180.");
  h += "<button class='principal'>Guardar</button></form>";

  h += "<form method='post' action='/config' class='card' onsubmit=\"return confirm('Repor todos os valores por defeito?')\">"
       "<input type='hidden' name='reset' value='1'><div class='muted'>Por defeito: vento 25, rajadas 30, chuva 0,2, "
       "60 min entre movimentos, vento ignora esse tempo, STOP aos 23,5 s, toque de 100 ms, noite das 20h às 7h, aeroporto de Lisboa.</div>"
       "<button>Repor valores por defeito</button></form>";
  h += "<p><a href='/'>← Voltar</a></p></body></html>";

  server.send(200, "text/html; charset=utf-8", h);
}

// Le um numero em texto; se faltar ou estiver fora dos limites, fica o valor atual.
float validar(String s, float atual, float minimo, float maximo, bool& rejeitado){
  s.trim();
  s.replace(',', '.');
  if(!s.length()) return atual;
  float v = s.toFloat();
  if(isnan(v) || v < minimo || v > maximo){ rejeitado = true; return atual; }
  return v;
}

// Aplica e guarda os valores. Devolve false se algum foi ignorado por estar fora dos limites.
bool aplicarConfig(const CfgTexto& c){
  bool rej = false;
  VENTO_MAX       = validar(c.vento,  VENTO_MAX,  5, 100, rej);
  RAJADA_MAX      = validar(c.rajada, RAJADA_MAX, 5, 150, rej);
  CHUVA_MIN       = validar(c.chuva,  CHUVA_MIN,  0, 20,  rej);
  INTERVALO_MIN_S = (long)lroundf(validar(c.interv, INTERVALO_MIN_S / 60.0f, 0, 360, rej) * 60);
  STOP_APOS_MS    = (unsigned long)lroundf(validar(c.stop, STOP_APOS_MS / 1000.0f, 0, 120, rej) * 1000);
  PULSO_MS        = (int)lroundf(validar(c.pulso, PULSO_MS, 50, 2000, rej));
  NOITE_INICIO    = (int)lroundf(validar(c.nini, NOITE_INICIO, 0, 23, rej));
  NOITE_FIM       = (int)lroundf(validar(c.nfim, NOITE_FIM,    0, 23, rej));
  LAT             = validar(c.lat, LAT, -90, 90, rej);
  LON             = validar(c.lon, LON, -180, 180, rej);
  VENTO_IGNORA_LIMITE = c.vign;
  guardarConfig();
  Serial.printf("Config: vento>%.1f rajadas>%.1f chuva>=%.1f intervalo=%ld min vento-ignora=%d stop=%lu ms pulso=%d ms noite=%dh-%dh\n",
                VENTO_MAX, RAJADA_MAX, CHUVA_MIN, INTERVALO_MIN_S / 60, VENTO_IGNORA_LIMITE, STOP_APOS_MS, PULSO_MS,
                NOITE_INICIO, NOITE_FIM);
  return !rej;
}

void guardarDaPagina(){
  if(server.hasArg("reset")){
    reporConfig();
    registar(NAN, NAN, NAN, "Configurações: valores por defeito");
    server.sendHeader("Location", "/config?ok=1");
    server.send(303);
    return;
  }
  CfgTexto c;
  c.vento = server.arg("vento");   c.rajada = server.arg("rajada"); c.chuva = server.arg("chuva");
  c.interv = server.arg("interv"); c.stop = server.arg("stop");     c.pulso = server.arg("pulso");
  c.lat = server.arg("lat");       c.lon = server.arg("lon");
  c.nini = server.arg("nini");     c.nfim = server.arg("nfim");
  c.vign = server.hasArg("vign");
  bool ok = aplicarConfig(c);
  registar(NAN, NAN, NAN, ok ? "Configurações alteradas" : "Configurações alteradas (alguns valores ignorados)");
  server.sendHeader("Location", ok ? "/config?ok=1" : "/config?ok=2");
  server.send(303);
}

// ---------------- arranque e ciclo ----------------
void setup(){
  pinMode(PIN_UP, OUTPUT);   digitalWrite(PIN_UP, LOW);
  pinMode(PIN_STOP, OUTPUT); digitalWrite(PIN_STOP, LOW);
  pinMode(PIN_DOWN, OUTPUT); digitalWrite(PIN_DOWN, LOW);
  Serial.begin(115200);
  delay(300);

  prefs.begin("toldo", false);   // estado guardado na memoria (sobrevive a falhas de luz)
  desconhecido = !prefs.isKey("estado");          // primeiro arranque?
  estado     = (Estado)prefs.getUChar("estado", DOWN);   // na duvida: DOWN (estendido)
  antesVento = (Estado)prefs.getUChar("antes", DOWN);
  emVento    = prefs.getBool("vento", false);
  ultimoMov  = (time_t)prefs.getUInt("ultMov", 0);
  carregarConfig();                                // valores mudados em /config
  if(desconhecido) Serial.println("Sem estado guardado: assumo DOWN e acerto na primeira verificacao.");
  else Serial.printf("Estado guardado: %s, modo vento: %s\n", nome(estado), emVento ? "sim" : "nao");

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(NOME_HOST);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("A ligar ao WiFi");
  while(WiFi.status() != WL_CONNECTED){ delay(500); Serial.print("."); }
  Serial.print(" ligado. IP: ");
  Serial.println(WiFi.localIP());

  configTzTime("WET0WEST,M3.5.0/1,M10.5.0", "pool.ntp.org");   // hora de Lisboa

  if(MDNS.begin(NOME_HOST)){
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Pagina: http://%s.local\n", NOME_HOST);
  }
  server.on("/", paginaInicial);
  server.on("/up",   HTTP_POST, [](){ botao(PIN_UP,   UP,   "UP"); });
  server.on("/stop", HTTP_POST, [](){ botao(PIN_STOP, DOWN, "STOP (conta como DOWN)"); });
  server.on("/down", HTTP_POST, [](){ botao(PIN_DOWN, DOWN, "DOWN"); });
  server.on("/config", HTTP_GET,  paginaConfig);
  server.on("/config", HTTP_POST, guardarDaPagina);
  server.begin();

  Serial.println("Comandos: u = subir  d = descer  v = verificar ja  e = ver estado");
}

void loop(){
  servir();

  // comandos manuais pelo monitor serie
  if(Serial.available()){
    char c = Serial.read();
    if(c == 'u'){ mover(UP);   registar(NAN, NAN, NAN, "Manual: UP"); }
    else if(c == 'd'){ mover(DOWN); registar(NAN, NAN, NAN, "Manual: DOWN"); }
    else if(c == 'v'){ ultimaHora = -1; ultimaTentativa = 0; }
    else if(c == 'e') Serial.printf("Estado: %s | modo vento: %s | antes do vento: %s\n",
                                    nome(estado), emVento ? "sim" : "nao", nome(antesVento));
  }

  if(!temHora()) return;   // ainda sem hora certa (NTP)
  time_t agora = time(nullptr);
  struct tm t; localtime_r(&agora, &t);

  // Uma verificacao por hora, as xx:02. A acao e feita logo a seguir.
  // Se falhar: nova tentativa de 2 em 2 min (ou logo, se entretanto mudar a hora).
  // Quando voltar a dar, fica registada esta hora e a proxima e na hora seguinte, as xx:02.
  // No arranque (ultimaHora = -1) e com 'v' verifica logo, sem esperar pelo minuto 2.
  bool horaDeVer = (t.tm_min >= MINUTO_VERIFICAR) || ultimaHora == -1;
  if(horaDeVer && t.tm_hour != ultimaHora &&
     (ultimaTentativa == 0 || millis() - ultimaTentativa >= REPETIR_MS || t.tm_hour != horaTentativa)){
    if(verificar()){ ultimaHora = t.tm_hour; ultimaTentativa = 0; horaTentativa = -1; }
    else { ultimaTentativa = millis(); horaTentativa = t.tm_hour; }
  }
}
