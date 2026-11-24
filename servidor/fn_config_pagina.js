// Pagina de configuracoes: mostra os valores que o ESP32 enviou e deixa muda-los.
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const fmt = ms => new Date(ms).toLocaleTimeString('pt-PT', { timeZone: 'Europe/Lisbon', hour: '2-digit', minute: '2-digit' });
const st = flow.get('toldo_estado') || null;
const pc = flow.get('toldo_cfg_pendente') || null;
const c = st && st.cfg;
const q = (msg.req && msg.req.query) || {};

const campo = (nome, rotulo, valor, ajuda) =>
    `<label><b>${rotulo}</b><input type="number" step="any" name="${nome}" value="${esc(valor)}"></label><div class="muted">${ajuda}</div>`;

let h = `<!doctype html><html lang="pt"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Toldo: configurações</title><style>
body{font-family:system-ui,sans-serif;margin:16px;max-width:640px;color:#222;background:#fafafa}
h1{font-size:22px;margin:0 0 12px}.card{background:#fff;border:1px solid #ddd;border-radius:10px;padding:12px;margin-bottom:12px}
label{display:block;margin-top:12px}input[type=number]{display:block;width:100%;box-sizing:border-box;padding:8px;font-size:16px;margin-top:4px;border:1px solid #bbb;border-radius:8px}
.muted{color:#777;font-size:13px;margin-top:2px}.ok{color:#1b7f3b;font-weight:600}.alerta{color:#b00020;font-weight:600}
button{padding:12px 18px;font-size:16px;font-weight:600;border-radius:10px;border:1px solid #bbb;background:#fff;cursor:pointer;margin-top:16px}
.principal{background:#1b5fb3;color:#fff;border-color:#1b5fb3}
</style></head><body><h1>Configurações</h1>`;

if (q.ok === '1') h += `<div class="card ok">Enviado. O ESP32 aplica as alterações nos próximos segundos. Recarrega a página para ver os valores em uso.</div>`;
if (pc) h += `<div class="card muted">Há alterações enviadas às ${esc(fmt(pc.quando))} à espera do ESP32.</div>`;

if (!c) {
    h += `<div class="card alerta">Ainda não há configurações do ESP32. Confirma que o ESP32 tem a versão mais recente do código e que está ligado.</div>`;
} else {
    h += `<form method="post" action="config" class="card">`;
    h += campo('vento',  'Vento médio (km/h)', c.vento,  'Recolhe se o vento médio previsto for acima deste valor. Entre 5 e 100.');
    h += campo('rajada', 'Rajadas (km/h)',     c.rajada, 'Recolhe se as rajadas previstas forem acima deste valor. Entre 5 e 150.');
    h += campo('chuva',  'Chuva (mm numa hora)', c.chuva, 'Estende se a chuva prevista for igual ou acima deste valor. Entre 0 e 20.');
    h += campo('interv', 'Tempo mínimo entre movimentos (min)', c.interv,
        'A automação não mexe o toldo mais do que uma vez neste intervalo. 0 = sem limite. Entre 0 e 360.');
    h += `<label><input type="checkbox" name="vign" value="1"${c.vign ? ' checked' : ''}> <b>O vento pode recolher o toldo mesmo antes desse tempo</b></label>`;
    h += campo('stop',  'STOP depois do UP (s)', c.stop,
        'No recolher por vento: segundos até ao STOP, para ficar um bocado de fora. 0 = recolhe tudo. Entre 0 e 120.');
    h += campo('pulso', 'Duração do toque nos botões (ms)', c.pulso, 'Entre 50 e 2000.');
    if (c.nini !== undefined) {   // so aparece se o ESP32 ja tiver a versao com modo noite
        h += campo('nini', 'Modo noite: início (hora)', c.nini,
            'A partir desta hora só mexe para recolher por vento ou estender com chuva. Entre 0 e 23.');
        h += campo('nfim', 'Modo noite: fim (hora)', c.nfim,
            'Até esta hora. Igual ao início = modo noite desligado. Entre 0 e 23.');
    }
    h += campo('lat',   'Latitude',  c.lat, 'Local da previsão. Entre -90 e 90.');
    h += campo('lon',   'Longitude', c.lon, 'Local da previsão. Entre -180 e 180.');
    h += `<button class="principal">Guardar</button></form>`;
    h += `<form method="post" action="config" class="card" onsubmit="return confirm('Repor todos os valores por defeito?')">
<input type="hidden" name="reset" value="1"><div class="muted">Por defeito: vento 25, rajadas 30, chuva 0,2, 60 min entre movimentos,
vento ignora esse tempo, STOP aos 23,5 s, toque de 100 ms, noite das 20h às 7h, aeroporto de Lisboa.</div>
<button>Repor valores por defeito</button></form>`;
}
h += `<p><a href="./">← Voltar</a></p></body></html>`;

msg.payload = h;
msg.headers = { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' };
return msg;
