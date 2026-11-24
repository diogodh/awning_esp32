// Guarda as alteracoes de configuracao para o ESP32 ir buscar (ele valida os valores).
const p = msg.payload || {};
let cfg;
if (p.reset) {
    cfg = { reset: true };
} else {
    cfg = { vign: p.vign === '1' };
    for (const k of ['vento', 'rajada', 'chuva', 'interv', 'stop', 'pulso', 'lat', 'lon', 'nini', 'nfim']) {
        if (p[k] !== undefined) cfg[k] = String(p[k]);
    }
}
flow.set('toldo_cfg_pendente', { cfg, quando: Date.now() });
node.status({ fill: 'blue', shape: 'dot', text: 'config pendente' });
msg.statusCode = 303;
msg.headers = { Location: '/config?ok=1', 'Cache-Control': 'no-store' };
msg.payload = '';
return msg;
