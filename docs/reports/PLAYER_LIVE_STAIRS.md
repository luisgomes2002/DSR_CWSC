# Subida e descida de escada — 2026-10-18

Captura de 30 segundos somente em leitura da instalação alternativa,
PID 17380, módulo em `0x140000000`. Foi solicitado ao usuário subir e descer
uma escada e parar embaixo e em cima. Não houve vídeo ou marcação individual
das ações; a interpretação comportamental assume a execução desse gesto.

Evidência: [captura JSONL](player-live-stairs-2026-10-18-01.jsonl).

## Resultado medido

- 234 amostras candidatas; 6 descartadas por `unstable_sample` (2,5% de
  240 tentativas), com recuperação e sem erro fatal.
- Uma única cadeia de endereços durante a captura; nenhuma falha nas
  conferências de identidade, tipos e âncoras do leitor.
- Intervalo médio entre amostras candidatas: 128,8 ms; máximo: 401,0 ms.
  Esses intervalos medem o leitor, não o ritmo de atualização interna do jogo.

| Componente de posição | Mínimo | Máximo | Amplitude |
| --- | ---: | ---: | ---: |
| 0 | 37,829178 | 50,120575 | 12,291397 |
| 1 | -119,198151 | -114,202209 | 4,995941 |
| 2 | 194,284256 | 207,894653 | 13,610397 |

Exemplos do componente 1 ao longo da captura, escolhendo a amostra mais
próxima de cada instante:

| Tempo (s) | Componente 1 |
| ---: | ---: |
| 0,226 | -119,080841 |
| 5,029 | -118,253029 |
| 10,010 | -114,315178 |
| 15,133 | -119,141594 |
| 19,990 | -115,987923 |
| 25,069 | -118,986908 |
| 30,039 | -118,400085 |

## Interpretação

O segundo componente da posição (índice 1) apresentou excursões de subida e
retorno, com amplitude de aproximadamente 5 unidades internas. No ensaio
anterior em [área plana](PLAYER_LIVE_FLAT_MOVEMENT.md), sua amplitude foi
apenas 0,085617. Essa diferença, no ensaio solicitado de escada, sustenta
identificar o índice 1 como eixo vertical candidato. Assumindo a subida do
piso inicial para o patamar superior, valores maiores correspondem a maior altura.

Não foi observada diretamente a ação na tela nem estabelecida equivalência
dessas unidades com metros. A orientação dos outros eixos, o zero e sentido
do ângulo, e as transições de menu/carregamento/morte continuam pendentes.
O leitor mantém nomes neutros de componentes até fechar a validação semântica.
Nenhuma memória do jogo foi escrita.

Próximo teste: voltar ao menu e carregar o personagem novamente, verificando
se o leitor sinaliza corretamente dados indisponíveis e recupera a cadeia,
sem reaproveitar valores antigos como atuais.
