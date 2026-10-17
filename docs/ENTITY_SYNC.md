# Entidades fictícias — marco 0.2

Este laboratório sincroniza IDs, nomes e posições numéricas de jogadores de
teste. Não cria personagens no DSR, não lê memória do jogo, não usa seus
pacotes e não acessa saves ou serviços oficiais.

## Teste manual da nova versão

A versão anterior estava aberta durante a compilação e não foi encerrada.
O executável novo está em `build-v02\dsr_net.exe`. Use esse executável tanto
no host quanto nos clientes. O protocolo agora é **2**; a versão 1 é recusada.

Abra três terminais PowerShell e execute em cada um:

```powershell
cd 'C:\Users\luisg\OneDrive\Documentos\dsr-network'
$env:DSR_ROOM_PASSWORD = 'lab-local-123'
```

Host, usando outra porta para não conflitar com o laboratório anterior:

```powershell
.\build-v02\dsr_net.exe host --port 27031 --max-players 4
```

Primeiro cliente:

```powershell
.\build-v02\dsr_net.exe join --port 27031 --name Luis
```

Segundo cliente:

```powershell
.\build-v02\dsr_net.exe join --port 27031 --name Piero
```

No cliente Luis:

```text
/move 10 0 5
/players
```

Os dois clientes devem receber algo como:

```text
POSITION    1    10    0    5    3
```

Os campos são: tipo, ID de quem se moveu, X, Y, Z e revisão global do mundo.
IDs e revisões variam com a sequência de entradas, movimentos e saídas.
`/move` define a posição absoluta; não soma deslocamento. Use ponto para
decimais, por exemplo `/move 10.5 -2 5`.

`/players` solicita uma fotografia completa e imprime `PLAYERS`, uma linha
`PLAYER` por entidade e `PLAYERS_END`. Um terceiro cliente recebe essa
fotografia automaticamente ao entrar, já com Luis em sua posição atual.

Teste também:

- Mover Piero: somente o jogador correspondente à conexão dele muda.
- `/move nan 0 0`, `/move 100001 0 0` ou `/move 1,5 0 0`: o cliente avisa
  `Comando invalido`, continua conectado e não envia a atualização inválida.
- `/quit`: os outros clientes recebem `LEFT` e `DESPAWN`; a entidade sai da lista.
- Entrar novamente: novo ID e posição inicial `(0, 0, 0)`, sem recuperar a entidade antiga.

Senha em texto claro e ausência de criptografia permanecem: somente testes
locais. Não abra portas no roteador. O host sozinho não cria um jogador;
para participar no PC do host, abra também um cliente.

## Regras da simulação

- Cada conexão admitida cria exatamente uma entidade de teste.
- O host escolhe o ID e a origem do movimento; `MOVE` não tem campo de ID.
- Cada coordenada é finita e fica no intervalo inclusivo `[-100000, 100000]`.
- Parsing e serialização numérica são independentes da configuração regional.
- Cada criação, movimento válido e remoção incrementa a revisão global.
- O host redistribui movimentos a todos, inclusive ao remetente.
- O cliente mantém uma réplica em memória. Um snapshot só substitui a réplica
  após conferir contagem, IDs únicos, valores e marcador final correspondente.
- Eventos com revisão antiga, entidade inexistente ou formato inválido são
  tratados como erro de protocolo pelo cliente, não aplicados silenciosamente.
- Snapshot e deltas percorrem o mesmo fluxo TCP ordenado. Não há protocolo
  UDP, previsão local, interpolação, reconciliação, física, velocidade máxima,
  persistência ou combate. Os limites numéricos não são um sistema anticheat.

## Mensagens novas do protocolo 2

Além dos comandos de sala/chat, continuam sendo linhas ASCII com LF e campos TAB:

| Direção | Campos |
| --- | --- |
| Cliente -> host | `MOVE X Y Z` |
| Cliente -> host | `PLAYERS` |
| Host -> cliente | `PLAYERS revisao_global quantidade` |
| Host -> cliente | `PLAYER id nome X Y Z revisao_do_jogador` |
| Host -> cliente | `PLAYERS_END revisao_global` |
| Host -> demais clientes | `SPAWN id nome X Y Z revisao_do_jogador` |
| Host -> todos os clientes | `POSITION id X Y Z revisao_do_jogador` |
| Host -> demais clientes | `DESPAWN id revisao_global` |

Os espaços na tabela representam separadores TAB. O comando `/move` do
terminal é convertido para essa representação; o usuário digita espaços.
`PLAYER`/`SPAWN` têm sete campos e `POSITION` tem seis, incluindo o verbo.

O cliente novo recebe `WELCOME`, roster e snapshot antes de processar os
eventos seguintes. Clientes já presentes recebem `JOIN` e `SPAWN`.
Na remoção, recebem `LEFT` e `DESPAWN`.

`MOVE` com quatro campos e coordenadas inválidas produz `ERROR BAD_POSITION`
sem desconectar nem alterar a posição/revisão. Comando com quantidade de
campos incorreta ou tentativa de enviar mensagens exclusivas do host é
recusado como `BAD_COMMAND` e encerra a conexão. Movimentação sem admissão
é recusada como `EXPECTED_HELLO`. Não há entidade criada para quem erra a senha.

## Organização do código

| Módulo | Responsabilidade |
| --- | --- |
| `world/player.hpp`, `world/world.hpp/.cpp` | Dados e estado autoritativo de entidades, sem sockets ou jogo |
| `protocol/entity_messages.hpp/.cpp` | Parsing tipado, limites e serialização das mensagens de entidades |
| `client/player_replica.hpp/.cpp` | Aplicação validada e atômica de snapshots e deltas no cliente |
| `ui/client_commands.hpp/.cpp` | Conversão dos comandos do terminal, incluindo validação local |
| `session/host.hpp/.cpp` | Liga a conexão admitida à entidade e coordena transporte/eventos |

## Compilar e testar

```powershell
cmake -S . -B build-v02 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-v02 --parallel 2
ctest --test-dir build-v02 --verbose
```

A suíte contém três grupos: os 14 cenários de rede existentes, 11 cenários
novos de sincronização em loopback e 8 casos unitários C++ para estado,
serialização, réplica, revisões e comandos. Não é teste de carga nem prova
de compatibilidade entre executáveis do DSR.
