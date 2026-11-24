// O ESP32 envia o estado e recebe o comando pendente (se houver).
const p = msg.payload;
if (p && typeof p === 'object') {
    flow.set('toldo_estado', p);
    flow.set('toldo_visto', Date.now());
}
let cmd = '';
const pend = flow.get('toldo_pendente');
if (pend) {
    if (Date.now() - pend.quando < 60000) cmd = pend.cmd;   // comandos com mais de 1 min sao descartados
    flow.set('toldo_pendente', null);
}
const resp = { cmd };
const pc = flow.get('toldo_cfg_pendente');
if (pc) {
    if (Date.now() - pc.quando < 600000) resp.cfg = pc.cfg;   // alteracoes com mais de 10 min sao descartadas
    flow.set('toldo_cfg_pendente', null);
}
node.status({ fill: 'green', shape: 'dot',
    text: 'ESP32 ' + new Date().toLocaleTimeString('pt-PT', { timeZone: 'Europe/Lisbon' }) +
          (cmd ? ' -> ' + cmd : '') + (resp.cfg ? ' -> config' : '') });
msg.payload = resp;
msg.headers = { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' };
return msg;
