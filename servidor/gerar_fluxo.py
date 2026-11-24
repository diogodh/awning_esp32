import json, pathlib
d = pathlib.Path(__file__).parent
f = lambda n: (d / n).read_text()
Z = "toldo_tab"
nodes = [
  {"id": Z, "type": "tab", "label": "Toldo", "disabled": False, "info": "Ponte entre toldo.exemplo.com e o ESP32."},
  {"id": "toldo_in_pag", "type": "http in", "z": Z, "name": "pagina", "url": "/toldo/", "method": "get",
   "upload": False, "swaggerDoc": "", "x": 130, "y": 80, "wires": [["toldo_fn_pag"]]},
  {"id": "toldo_fn_pag", "type": "function", "z": Z, "name": "montar pagina", "func": f("fn_pagina.js"),
   "outputs": 1, "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": 340, "y": 80, "wires": [["toldo_out_pag"]]},
  {"id": "toldo_out_pag", "type": "http response", "z": Z, "name": "", "statusCode": "", "headers": {}, "x": 540, "y": 80, "wires": []},

  {"id": "toldo_in_cmd", "type": "http in", "z": Z, "name": "botoes", "url": "/toldo/cmd", "method": "post",
   "upload": False, "swaggerDoc": "", "x": 130, "y": 160, "wires": [["toldo_fn_cmd"]]},
  {"id": "toldo_fn_cmd", "type": "function", "z": Z, "name": "guardar comando", "func": f("fn_cmd.js"),
   "outputs": 1, "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": 340, "y": 160, "wires": [["toldo_out_cmd"]]},
  {"id": "toldo_out_cmd", "type": "http response", "z": Z, "name": "", "statusCode": "", "headers": {}, "x": 540, "y": 160, "wires": []},

  {"id": "toldo_in_esp", "type": "http in", "z": Z, "name": "esp32", "url": "/toldo/esp", "method": "post",
   "upload": False, "swaggerDoc": "", "x": 130, "y": 240, "wires": [["toldo_fn_esp"]]},
  {"id": "toldo_fn_esp", "type": "function", "z": Z, "name": "estado + comando", "func": f("fn_esp.js"),
   "outputs": 1, "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": 340, "y": 240, "wires": [["toldo_out_esp"]]},
  {"id": "toldo_out_esp", "type": "http response", "z": Z, "name": "", "statusCode": "", "headers": {}, "x": 540, "y": 240, "wires": []},

  {"id": "toldo_in_cfgp", "type": "http in", "z": Z, "name": "config (ver)", "url": "/toldo/config", "method": "get",
   "upload": False, "swaggerDoc": "", "x": 140, "y": 320, "wires": [["toldo_fn_cfgp"]]},
  {"id": "toldo_fn_cfgp", "type": "function", "z": Z, "name": "pagina config", "func": f("fn_config_pagina.js"),
   "outputs": 1, "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": 340, "y": 320, "wires": [["toldo_out_cfgp"]]},
  {"id": "toldo_out_cfgp", "type": "http response", "z": Z, "name": "", "statusCode": "", "headers": {}, "x": 540, "y": 320, "wires": []},

  {"id": "toldo_in_cfgs", "type": "http in", "z": Z, "name": "config (guardar)", "url": "/toldo/config", "method": "post",
   "upload": False, "swaggerDoc": "", "x": 150, "y": 400, "wires": [["toldo_fn_cfgs"]]},
  {"id": "toldo_fn_cfgs", "type": "function", "z": Z, "name": "guardar config", "func": f("fn_config_guardar.js"),
   "outputs": 1, "timeout": 0, "noerr": 0, "initialize": "", "finalize": "", "libs": [], "x": 340, "y": 400, "wires": [["toldo_out_cfgs"]]},
  {"id": "toldo_out_cfgs", "type": "http response", "z": Z, "name": "", "statusCode": "", "headers": {}, "x": 540, "y": 400, "wires": []},
]
(d / "toldo_nodered_flow.json").write_text(json.dumps(nodes, ensure_ascii=False, indent=1))
print("ok", len(nodes))
