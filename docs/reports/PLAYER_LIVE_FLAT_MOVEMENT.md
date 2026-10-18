# Movimento e direção em área plana — 2026-10-18

Captura somente em leitura, de 30 segundos, da instalação alternativa no
PID 17380, base `0x140000000`. Foi solicitado ao usuário deslocamento em linha
reta, mudanças de direção e parada final em área plana. As ações individuais
não foram marcadas nem filmadas.

Evidência: [JSONL completo](player-live-flat-movement-2026-10-18-01.jsonl).

## Resultado

- 234 amostras candidatas e 3 descartadas por `unstable_sample` (1,27% das
  237 tentativas); recuperação automática, sem erro fatal.
- 225 posições distintas e 212 orientações distintas.
- Uma única cadeia de objetos; as conferências do leitor não detectaram
  identidade, tipo ou âncoras incompatíveis.
- Intervalo médio entre amostras candidatas: 128,3 ms; máximo: 386,6 ms.
  São intervalos do leitor externo, não medições do ritmo interno do jogo.

| Componente | Mínimo | Máximo | Amplitude |
| --- | ---: | ---: | ---: |
| Posição 0 | 43,272503 | 49,028946 | 5,756443 |
| Posição 1 | -119,166451 | -119,080833 | 0,085617 |
| Posição 2 | 203,098312 | 208,592285 | 5,493973 |
| Orientação 0 | 0 | 0 | 0 |
| Orientação 1 (rad, candidata) | -3,132679 | 3,128911 | 6,261590 |
| Orientação 2 | 0 | 0 | 0 |

## Interpretação e próximo ensaio

Os componentes 0 e 2 da posição variaram amplamente, enquanto o componente 1
variou pouco. Isso é compatível com 0/2 no plano horizontal e 1 como altura;
a confirmação exige uma subida e descida controladas. As unidades e a
orientação dos eixos no mundo ainda não foram determinadas.

Somente o componente 1 da orientação variou, aproximando-se dos limites
`-pi` e `+pi`. Junto à [captura solicitada apenas com a câmera](PLAYER_LIVE_CAMERA_ONLY.md),
é evidência compatível com ângulo de direção horizontal do personagem.
A ausência de mudança naquela captura depende de o usuário ter efetivamente
girado a câmera durante a janela. O sentido e a direção zero do ângulo ainda
precisam de referência controlada; mínimo/máximo não comprovam uma volta completa.

Não houve escrita na memória. Próximo ensaio: subir e descer uma escada,
incluindo pausas embaixo e em cima, para testar o componente vertical candidato.
