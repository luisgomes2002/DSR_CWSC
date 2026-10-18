# Retorno ao menu e carregamento — 2026-10-18

Captura de 60 segundos somente em leitura da instalação alternativa,
PID 17380, base `0x140000000`. O usuário foi orientado a voltar ao menu,
aguardar, carregar o personagem novamente e movimentá-lo após carregar.
Não houve vídeo ou marcação independente das telas do jogo.

Evidência: [JSONL completo](player-live-reload-2026-10-18-01.jsonl).

## Resultado: recuperação observada

591 tentativas produziram 479 amostras candidatas e 112 registros
de indisponibilidade esperada na transição. A captura terminou normalmente,
sem erro fatal de identidade, âncoras ou leitura. As indisponibilidades
foram 104 `null_world` e 8 `null_player`, sem `unstable_sample` nesta captura.

| Tempo desde o início (s) | Estado observado | Registros |
| --- | --- | ---: |
| 0,182–8,277 | Candidatos disponíveis, geração 1 | 75 |
| 8,286 | Ponteiro do jogador nulo | 1 |
| 8,377–18,713 | Ponteiro do mundo nulo | 104 |
| 18,817–19,417 | Ponteiro do jogador nulo | 7 |
| 19,559–60,118 | Candidatos disponíveis, geração 2 | 404 |

Da primeira indisponibilidade à primeira amostra recuperada decorreram
aproximadamente 11,27 segundos. Esse intervalo inclui a transição realizada
pelo usuário e o carregamento; não é uma medição isolada da latência de
recuperação após o jogo ficar pronto.

| Objeto | Antes | Depois |
| --- | --- | --- |
| Mundo | `0xd0d9b80` | `0x1bbdcb10` |
| Jogador | `0xcc55400` | `0x1b8d1fb0` |
| Controlador | `0xcc56470` | `0x1b8d3020` |
| Movimento | `0xcc56e10` | `0x1b8d39c0` |

O leitor reobteve a cadeia completa e passou à geração local 2, sem
reinício ou nova anexação ao processo. Todos os 112 registros indisponíveis
omitiram posição, orientação e endereços; nenhum apresentou valores antigos
como se fossem uma amostra atual. A posição inicial recuperada coincidiu
com a anterior ao menu, mas veio da nova cadeia validada. Após a retomada,
foram observadas 101 posições distintas.

## Alcance

Este ensaio confirma recuperação nessa única transição observada da
instalação alternativa. Não comprova prontidão do mundo em todas as telas
de carregamento, recuperação após morte, troca de área/personagem, repetição
prolongada nem compatibilidade da instalação Steam. O identificador de script
continua sem validação como identidade de rede. Nenhuma memória foi escrita.

Próximo ensaio de ciclo de vida: captura durante morte e reaparecimento,
com ação manual do usuário. Antes de transmitir dados reais, o adaptador
deve preservar a indisponibilidade e a mudança de geração, sem publicar
valores anteriores como movimento atual.
