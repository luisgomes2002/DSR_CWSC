# Primeira leitura do jogo em execução — 2026-10-18

Instalação alternativa informada pelo usuário:
`E:\DARK SOULS REMASTERED\DARK SOULS REMASTERED\DarkSoulsRemastered.exe`.
Processo observado: PID 17380; base do módulo: `0x140000000`.

O leitor conferiu o hash do executável, a identidade do módulo, as âncoras de
código e os tipos e vínculos da cadeia de objetos. Usou somente acesso de leitura.
A tentativa dentro do ambiente restrito falhou ao consultar os módulos por
acesso negado; a execução autorizada fora dessa restrição concluiu a captura.

Em uma captura configurada para 8 segundos a 10 Hz, foram obtidas 74 amostras
candidatas, sem amostras indisponíveis. Todas apresentaram os mesmos valores:

- Posição candidata: `(44.08147430419922, -119.08084106445312, 206.00320434570312)`.
- Orientação candidata em radianos: `(0, 1.339059829711914, 0)`.
- Identificador de script candidato: `0`; isso não comprova identidade de rede.

A cadeia permaneceu na geração local 1. Esses resultados demonstram leitura
consistente dos candidatos nesta execução. A captura, solicitada com o personagem
parado, não comprova ainda correspondência com movimento, ordem dos eixos,
unidades, direção do personagem ou comportamento em carregamentos e morte.
A instalação Steam também continua sem validação em execução.

Próxima validação: capturar deslocamento e rotação controlados do personagem,
comparar as mudanças dos componentes com as ações observadas e depois verificar
transições de estado.

Evidências preservadas:

- [Captura concluída](player-live-alternate-2026-10-18-02.jsonl).
- [Tentativa restrita](player-live-alternate-2026-10-18-01.jsonl).
- [Guia do leitor](../PLAYER_READER.md).
