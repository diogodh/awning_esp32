// Monta a pagina do toldo com o ultimo estado enviado pelo ESP32.
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const fmt = (ms, o) => new Date(ms).toLocaleString('pt-PT', Object.assign({ timeZone: 'Europe/Lisbon' },
    o || { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' }));

const st = flow.get('toldo_estado') || null;
const visto = flow.get('toldo_visto') || 0;
const pend = flow.get('toldo_pendente') || null;
const agora = Date.now();
const online = visto && (agora - visto) < 30000;

let h = `<!doctype html><html lang="pt"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="10"><title>Toldo</title><style>
body{font-family:system-ui,sans-serif;margin:16px;max-width:640px;color:#222;background:#fafafa}
h1{font-size:22px;margin:0 0 12px}.card{background:#fff;border:1px solid #ddd;border-radius:10px;padding:12px;margin-bottom:12px}
table{width:100%;border-collapse:collapse;font-size:14px}th,td{padding:6px 4px;border-bottom:1px solid #eee;text-align:left}
th{color:#666;font-weight:600}.alerta{color:#b00020;font-weight:600}.ok{color:#1b7f3b;font-weight:600}.muted{color:#777;font-size:13px}
.botoes{display:flex;gap:8px}.botoes form{flex:1;margin:0}
button{width:100%;padding:16px 0;font-size:18px;font-weight:600;border-radius:10px;border:1px solid #bbb;background:#fff;cursor:pointer}
button:active{background:#eee}.stop{border-color:#b00020;color:#b00020}
</style></head><body><h1>Toldo</h1>
<div class="card botoes">
<form method="post" action="cmd"><input type="hidden" name="cmd" value="up"><button>&#9650; UP</button></form>
<form method="post" action="cmd"><input type="hidden" name="cmd" value="stop"><button class="stop">&#9632; STOP</button></form>
<form method="post" action="cmd"><input type="hidden" name="cmd" value="down"><button>&#9660; DOWN</button></form>
</div><div class="card">`;

if (online) h += `<div class="ok">ESP32 ligado (último contacto há ${Math.round((agora - visto) / 1000)} s)</div>`;
else if (visto) h += `<div class="alerta">ESP32 sem contacto desde ${esc(fmt(visto))}</div>`;
else h += `<div class="alerta">O ESP32 ainda não contactou o servidor.</div>`;

if (pend) h += `<div class="muted">Comando à espera do ESP32: ${esc(pend.cmd.toUpperCase())}</div>`;

if (st) {
    h += `<div><b>Estado:</b> ${esc(st.estado)}${st.desconhecido ? ' (ainda por confirmar)' : ''}</div>`;
    h += `<div><b>Modo vento:</b> ${st.emVento ? 'sim (antes estava ' + esc(st.antesVento) + ')' : 'não'}</div>`;
    if (st.falhaPrevisao) h += `<div class="alerta">Previsão em falta. Nova tentativa às ${esc(st.proxima)}</div>`;
    else h += `<div><b>Próxima verificação:</b> ${esc(st.proxima)}</div>`;
    const c = st.cfg;
    if (c) {
        h += `<div class="muted">Limites: vento &gt; ${esc(c.vento)} km/h ou rajadas &gt; ${esc(c.rajada)} km/h → UP. ` +
             `Chuva ≥ ${esc(c.chuva)} mm → DOWN.` +
             (Number(c.interv) > 0 ? ` No máximo 1 movimento automático a cada ${esc(c.interv)} min` +
              (c.vign ? ' (exceto recolher por vento)' : '') + '.' : '') + `</div>`;
    } else {
        h += `<div class="muted">Limites: vento / rajadas acima de ${esc(st.ventoMax)} km/h → UP. Chuva ≥ ${esc(st.chuvaMin)} mm → DOWN.</div>`;
    }
}
if (flow.get('toldo_cfg_pendente')) h += `<div class="muted">Alterações de configuração à espera do ESP32.</div>`;
h += `<div style="margin-top:6px"><a href="config">Configurações</a></div>`;
h += `</div><div class="card"><b>Últimas 6 horas</b><table><tr><th>Hora</th><th>Vento</th><th>Rajadas</th><th>Chuva</th><th>Ação</th></tr>`;

const hist = (st && Array.isArray(st.hist)) ? st.hist : [];
const n = v => (v === undefined || v === null) ? '-' : esc(v);
for (const r of hist) {
    h += `<tr><td>${esc(fmt(r.t * 1000))}</td><td>${n(r.v)} km/h</td><td>${n(r.r)} km/h</td><td>${n(r.c)} mm</td><td>${esc(r.a)}</td></tr>`;
}
if (!hist.length) h += `<tr><td colspan="5" class="muted">Sem registos nas últimas 6 horas.</td></tr>`;
h += `</table><div class="muted">Previsão Open-Meteo. A página atualiza a cada 10 segundos.</div></div></body></html>`;

msg.payload = h;
msg.headers = { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' };
return msg;
