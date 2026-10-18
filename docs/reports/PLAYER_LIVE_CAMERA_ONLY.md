# Captura solicitada: somente câmera — 2026-10-18

Foi solicitado ao usuário manter o personagem parado e girar somente a câmera
durante uma captura de 30 segundos. O leitor acessou somente em leitura a
instalação alternativa, PID 17380, base `0x140000000`.

Evidência: [captura JSONL](player-live-camera-only-2026-10-18-01.jsonl).

## Resultado

- 269 amostras candidatas; nenhuma indisponível e nenhum erro fatal.
- Posição idêntica em todas as amostras:
  `(45.580772399902344, -119.08158111572266, 204.95997619628906)`.
- Orientação candidata idêntica em todas as amostras:
  `(0, 1.5303349494934082, 0)` radianos.
- Intervalo médio entre amostras: 111,9 ms; máximo: 197,4 ms.
  Esses intervalos medem o leitor externo, não a atualização interna do jogo.

O ângulo candidato que variou na [captura de movimento](PLAYER_LIVE_MOVEMENT.md)
não variou neste ensaio solicitado apenas com a câmera. Se a câmera foi girada
durante a janela de captura, isso é evidência de que esse campo não representa
a orientação da câmera, sendo compatível com orientação do personagem.

Não houve captura de vídeo, leitura independente da câmera nem confirmação
posterior do usuário sobre a execução do gesto. Portanto, a ausência de variação
está medida, mas a distinção comportamental depende desse gesto ter ocorrido.
O sentido e o zero do ângulo, os eixos da posição e as transições de estado
continuam pendentes. Próximo ensaio: deslocamento e mudança de direção do
personagem em área plana, com ações marcadas.
