# Retomar os testes — próxima sessão

Ponto de parada registrado em 18/10/2026. Próxima sessão prevista: 19/10/2026.
Nenhum teste adicional precisa ser executado hoje.

## O que já foi observado

Na instalação alternativa, o leitor externo obteve dados em repouso, durante
movimento e nas escadas. O segundo componente da posição é compatível com
altura; o segundo da orientação é compatível com a direção horizontal do
personagem. A captura solicitada apenas com a câmera manteve esses valores
constantes. Os gestos não foram filmados ou marcados individualmente; os
eixos, unidades e convenções angulares ainda não estão totalmente confirmados.

O último teste, voltar ao menu e carregar novamente, concluiu com recuperação
automática: 479 amostras candidatas e 112 registros indisponíveis durante a
transição. A cadeia de objetos mudou e nenhum registro indisponível publicou
posição antiga. Após a retomada, houve movimento nos dados.

Evidências:

- [Repouso](reports/PLAYER_LIVE_INITIAL.md).
- [Somente câmera](reports/PLAYER_LIVE_CAMERA_ONLY.md).
- [Movimento em área plana](reports/PLAYER_LIVE_FLAT_MOVEMENT.md).
- [Escada / altura](reports/PLAYER_LIVE_STAIRS.md).
- [Menu e carregamento — último teste concluído](reports/PLAYER_LIVE_RELOAD.md).

## Preparar a próxima sessão

1. Abra uma instalação por vez e carregue o personagem.
2. Abra o terminal na pasta deste projeto.
3. Localize o processo atual. O PID 17380 era da sessão de hoje e não deve ser
   reutilizado sem conferir.

```powershell
Get-Process -Name DarkSoulsRemastered | Select-Object Id, Path
```

Para continuar com a instalação alternativa, configure os valores abaixo.
Substitua `12345` pelo PID mostrado e confira o caminho do processo:

```powershell
$gameProcessId = 12345
$gameExe = 'E:\DARK SOULS REMASTERED\DARK SOULS REMASTERED\DarkSoulsRemastered.exe'
```

O leitor não abre o jogo nem movimenta o personagem. As ações são manuais.
Os comandos abaixo criam arquivos novos com data/hora para preservar os resultados.

## 1. Morte e reaparecimento — próximo teste

Reserve até 3 minutos e escolha uma situação de teste apropriada no jogo.

1. Inicie a captura com o personagem vivo e parado por alguns segundos.
2. Faça o personagem morrer e aguarde o reaparecimento.
3. Depois de reaparecer, fique parado, ande e mude de direção.
4. Anote aproximadamente quando morreu e quando voltou a controlar o personagem.

```powershell
$captureTag = Get-Date -Format 'yyyyMMdd-HHmmss'
python tools/player_reader.py --pid $gameProcessId --exe $gameExe --seconds 180 --include-script-id --output "docs/reports/player-live-death-$captureTag.jsonl"
```

Verificar: ausência de erro fatal; registros sem valores antigos quando a
cadeia ficar indisponível; retomada de posição/orientação após reaparecer;
mudanças de endereço e geração. A morte pode conservar objetos por algum
tempo: não exigir ponteiro nulo como condição para passar. Se o leitor continuar
emitindo valores durante telas de carregamento, registrar essa limitação e
investigar um sinal independente de prontidão antes de transmitir movimento.

## 2. Troca de área com carregamento

Faça uma transição disponível no jogo que realmente provoque carregamento,
preferencialmente ida e volta. Anote os momentos das transições.

```powershell
$captureTag = Get-Date -Format 'yyyyMMdd-HHmmss'
python tools/player_reader.py --pid $gameProcessId --exe $gameExe --seconds 180 --include-script-id --output "docs/reports/player-live-area-$captureTag.jsonl"
```

Verificar: recuperação após cada transição, tratamento de objetos recriados,
ausência de posições antigas nos registros indisponíveis e retomada do movimento.
Não interpretar saltos de coordenadas entre áreas como velocidade física.

## 3. Encerramento do jogo durante a captura

Inicie a captura, aguarde alguns segundos e encerre o jogo normalmente.

```powershell
$captureTag = Get-Date -Format 'yyyyMMdd-HHmmss'
python tools/player_reader.py --pid $gameProcessId --exe $gameExe --seconds 60 --output "docs/reports/player-live-exit-$captureTag.jsonl"
```

Verificar: registro `stopped` com motivo `process_exited`, encerramento do leitor
e ausência de reconexão automática a outro processo. A detecção de saída já
foi testada com processo sintético; falta essa confirmação no jogo real.

## 4. Repetição e outra instalação

- Repetir menu/carregamento e morte/reaparecimento algumas vezes para verificar
  se a recuperação se mantém. Um sucesso não cobre todos os estados do jogo.
- Quando for conveniente, repetir repouso, câmera, movimento, escada e transições
  separadamente na instalação Steam. O executável em disco é igual, mas os
  módulos instalados diferem; a validação atual cobre apenas a alternativa.
- Ao reabrir o jogo, consultar novamente o PID. Para a Steam, selecionar o
  caminho `E:\SteamLibrary\steamapps\common\DARK SOULS REMASTERED\DarkSoulsRemastered.exe`
  e usar nomes novos de relatório que indiquem essa instalação.
- LAN com dois PCs continua pendente e pode ficar para quando os dois estiverem
  disponíveis; os ensaios do leitor não substituem esse teste de rede.

## Depois dos testes de ciclo de vida

Confirmar sentido e zero do ângulo, orientação dos eixos horizontais e unidades
antes de converter os componentes para o protocolo de movimento. O identificador
de script observado em 0 continua sem validação como identidade de rede.

Só então preparar o adaptador de leitura para o laboratório de rede, preservando
indisponibilidade e mudanças de geração. Os dados reais ainda não são enviados
pela rede e não há representação de outro jogador dentro do jogo.

Para retomar com assistência, diga: **“Vamos continuar pelo teste de morte e
reaparecimento do docs/RETOMAR_TESTES.md; abri a instalação [alternativa/Steam].”**

Guia técnico e limites do leitor: [PLAYER_READER.md](PLAYER_READER.md).
