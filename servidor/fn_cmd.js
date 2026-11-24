// Botao da pagina: guarda o comando para o ESP32 ir buscar.
const c = String((msg.payload && msg.payload.cmd) || '').toLowerCase();
if (['up', 'down', 'stop'].includes(c)) {
    flow.set('toldo_pendente', { cmd: c, quando: Date.now() });
    node.status({ fill: 'blue', shape: 'dot', text: 'pendente: ' + c });
}
msg.statusCode = 303;
msg.headers = { Location: '/', 'Cache-Control': 'no-store' };
msg.payload = '';
return msg;
