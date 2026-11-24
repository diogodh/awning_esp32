# DD27xx / Gaviota Elite 60090165 — resumo da fase RF (atualizado 2026-09-27)

## Estado: pesquisa concluída
Objetivo: controlar o motor Elite por RF (ESP32-C6 + CC1101 E07-M1101D).
**Conclusão: replay/síntese por RF NÃO é viável** para este motor bidirecional cifrado.
(Depois desta fase, optou-se por opto-acopladores nos botões de um comando real — ver README.md.)

## Confirmado empiricamente no próprio Gaviota Elite (RX com CC1101)
- 2-FSK, 38 383 baud, sync 0x2DD4 (16/16), desvio ~5 kHz.
- Portadora real ~433,9357 MHz (+15,9 kHz face a 433,920). Registo FREQ = 10 B0 99.
- Frame (~22 bytes úteis + zeros de enchimento): `2D D4` (sync) → `24 25` (cabeçalho constante em todos os frames) → ~18 bytes cifrados.
- Rolling code agressivo: muda a cada ciclo (~150 ms). Entre codes distintas, Hamming ~44–50 % no payload; sem byte-contador visível → cifrado.
- Sinal do comando ~−27 a −46 dBm; ruído de fundo ~−110. Filtro RSSI ≥ −75 + byte2 == 0x24 limpa tudo.

## Teste de replay — FALHOU
- Capturado com o motor desligado; reenviado o código "próximo", com preâmbulo de 24 bytes e 5 repetições, a 433,9357 MHz.
- O motor não reagiu a nenhuma variante. Confirma anti-replay / rolling code.

## Alternativas estudadas
- Integração "Gaviota" no Home Assistant (ecossistema Coulisse/Motionblinds), com uma ponte Wi-Fi↔RF:
  - Módulo CONNECT Gaviota 60090167 (~170 €, garantido);
  - Motionblinds CMD-01 (~125 €) ou CM-20 (~60–90 €), emparelhamento provável mas não confirmado.
- Opto-acopladores nos botões de um comando real (~30–50 €) — o caminho escolhido.

## Ferramentas da fase RF (ESP32-C6 + E07-M1101D)
- Ligações: GDO0=GPIO14, CSN=GPIO18, SCK=GPIO21, MOSI=GPIO19, MISO/GDO1=GPIO20, VCC=3V3.
- `receptor.ino`, `replay_teste.ino`, `analisa.py` (não incluídos; já não são usados).
