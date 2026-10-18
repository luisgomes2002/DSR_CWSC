# Leitura durante movimento — 2026-10-18

Captura somente em leitura da instalação alternativa, PID 17380, base
`0x140000000`, por 30 segundos. Foi solicitado ao usuário andar, mudar de
direção e parar. As ações não foram filmadas nem marcadas individualmente;
a correlação exata entre cada ação e os componentes ainda requer ensaio controlado.

Evidência: [JSONL completo](player-live-alternate-2026-10-18-03.jsonl).
Comparação: [captura inicial estável](PLAYER_LIVE_INITIAL.md).

## Resultado observado

- 223 tentativas: 217 amostras candidatas e 6 indisponíveis (2,69%).
- Todas as indisponibilidades foram `unstable_sample`: as leituras repetidas
  não produziram um bloco idêntico dentro das três tentativas internas.
  O leitor descartou esses dados e voltou a obter amostras candidatas.
- 174 posições distintas e 107 orientações distintas.
- Uma única cadeia de endereços durante toda a captura. A geração local
  chegou a 6 após retomadas; esse contador não indica seis jogadores ou
  recriação comprovada de objetos.
- Identificador de script candidato permaneceu em 0; não validado como
  identificador de jogador para o protocolo de rede.
- Conferências de identidade, tipos e âncoras concluídas sem erro fatal.

| Componente | Mínimo | Máximo |
| --- | ---: | ---: |
| Posição 0 | 38,630035 | 49,133900 |
| Posição 1 | -119,200615 | -117,647308 |
| Posição 2 | 202,059921 | 209,755600 |
| Orientação 0 | 0 | 0 |
| Orientação 1 (rad, candidata) | -3,125133 | 2,567263 |
| Orientação 2 | 0 | 0 |

O componente 1 da orientação é compatível com um ângulo de direção horizontal,
mas o sentido, o zero e a associação aos eixos não foram confirmados.
Não houve interpretação como quaternion nem conversão para um pacote de rede.

## Cadência e limites

Apesar de configurada para 10 Hz, a captura produziu aproximadamente 7,4
tentativas por segundo. O intervalo médio entre amostras candidatas foi
138,5 ms e o maior foi 495,0 ms. Esses números medem a captura externa,
incluindo escalonamento e validações; não demonstram pausas equivalentes
nas atualizações internas do jogo. Não foi medida aqui a causa da cadência.

O resultado fortalece os candidatos de posição e orientação: houve leitura
estável em repouso na captura anterior e variação nesta captura, com a mesma
cadeia de objetos. Ainda faltam marcações controladas de deslocamento,
rotação do personagem versus câmera e subida/descida para fechar a semântica.
Menus, carregamento, morte, recriação do personagem e a instalação Steam
continuam sem validação em execução. Não houve escrita na memória do jogo.
