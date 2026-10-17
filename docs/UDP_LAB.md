# Movimentos em UDP protegido — laboratorio 0.4

Executavel: `build-v04/dsr_net.exe`. Nao abre nem altera Dark Souls, Steam,
saves, firewall ou roteador. Binarios anteriores foram preservados. Todos os
participantes devem usar a build 0.4. Framing TCP 3, build `dsr-network-lab-0.4`.

## Canais e confianca

- TCP/TLS: admissao, senha, roster, SPAWN/DESPAWN, chat, snapshots e QUIT.
- `--udp` nos dois lados: propostas/estados de posicao em DTLS 1.2/OpenSSL,
  com o [payload binario v1 de 68 bytes](MOVEMENT_PROTOCOL.md).
- PSK de 32 bytes pelo TLS exporter da conexao autenticada. Label exata
  `EXPORTER-dsr-network-movement-v1` (sem NUL), contexto `session || connection`
  (32 bytes binarios), `use_context=1`. Exige TLS Extended Master Secret.
- A PSK nao e a senha da sala. Nao e impressa, gravada ou transmitida como
  campo de protocolo. Copias locais de trabalho sao apagadas, sem prometer
  ausencia de segredos na RAM. Nova entrada tem nova connection e nova chave.
- DTLS usa `PSK-AES128-GCM-SHA256`, identidade `dsrm-1`, MTU 1200, sem tickets
  ou renegociacao. OpenSSL executa criptografia, autenticacao e anti-replay.
  Nao criamos algoritmos criptograficos proprios. Nao houve auditoria de seguranca.
- O pin do certificado continua sendo a ancora de confianca do TLS; o DTLS
  prova posse da chave derivada daquele TLS. IDs sozinhos nao autenticam nada.

## Host: primeiro PowerShell

Reutilize o certificado criado no [guia TLS](TLS_LAB.md), se ainda valido.
Caso ainda nao tenha certificado, siga primeiro a secao de geracao daquele guia.
O gerador exige pasta nova e nao sobrescreve identidades existentes.

```powershell
cd 'C:\Users\luisg\OneDrive\Documentos\dsr-network'
$dsrIdentity = Join-Path $env:LOCALAPPDATA 'DSRNetworkLab\host-v03'
$env:DSR_TLS_PFX_PASSWORD = 'COLOQUE_A_SENHA_USADA_AO_GERAR_O_PFX'
$env:DSR_ROOM_PASSWORD = 'sala-teste-123'
.\build-v04\dsr_net.exe host --port 27034 --max-players 4 --tls-pfx "$dsrIdentity\host.pfx" --udp
```

Substitua a senha PFX pelo valor real e confira o caminho. Ela e diferente da
senha da sala. Copie os 64 digitos de `TLS_PIN` para os clientes por canal
confiavel. So o host precisa de PFX e senha PFX: nao compartilhe esses segredos.

## Cliente: outro PowerShell

```powershell
cd 'C:\Users\luisg\OneDrive\Documentos\dsr-network'
$env:DSR_ROOM_PASSWORD = 'sala-teste-123'
$dsrPin = 'COLE_AQUI_O_TLS_PIN_REAL_DE_64_DIGITOS'
.\build-v04\dsr_net.exe join --port 27034 --name Piero --tls-pin $dsrPin --udp
```

Troque o placeholder pelo pin real, nao pela senha. Aguarde `UDP_ACTIVE`.
Em um terceiro terminal, conecte outro cliente com `--name Amigo`.
O host nao e um jogador; cada `join` ocupa uma vaga, inclusive no mesmo PC.

No cliente Piero, digite `/move 10 0 5`. Os clientes devem receber
`UDP_POSITION`, ID, coordenadas e revisao. Espere esse resultado e digite
`/players`: o snapshot TCP deve mostrar a mesma posicao aceita no host.
Chat, `/who`, `/send ID texto` e `/quit` permanecem no TCP/TLS.

Depois de `/move`, o cliente repete a ultima posicao desejada em novas amostras
(no maximo aproximadamente 29,4/s), mesmo parado, ate sair ou mudar de posicao.
Isso recupera a perda do ultimo datagrama; nao e uma trajetoria automatica.
O host incrementa a revisao apenas quando a posicao quantizada muda. Republica
estados absolutos em round-robin; o terminal imprime apenas revisoes novas.
Teste saida, reconexao e queda do host: nao ha reconexao automatica/migracao.

## Negociacao no controle TLS

```text
cliente -> host: UDP_REQUEST<TAB>1<TAB>porta_udp_do_cliente
host -> cliente: UDP_OFFER<TAB>1<TAB>session_hex32<TAB>connection_hex32<TAB>porta_udp_do_host
```

Os dois derivam a PSK e iniciam DTLS. O host envia `UDP_READY<TAB>1` pelo TLS
apos seu handshake. O cliente anuncia `UDP_ACTIVE<TAB>1` so quando o DTLS
local e a confirmacao do host estao prontos. Negociacao duplicada e recusada.

Um socket UDP por participante, na interface local efetiva do TCP e porta
efemera. O par e fixado ao IP do TCP e a porta recebida dentro do TLS.
Portas remotas abaixo de 1024 sao recusadas. Nao aprendemos endpoint com
datagrama nao autenticado. Nao existe uma unica porta UDP fixa neste marco.
Sem NAT rebinding, hole punching ou descoberta de porta publica: esse desenho
destina-se a loopback/LAN e pode nao funcionar em VPNs/NAT.

O host valida propriedade da entidade e sequencia crescente. State usa a
revisao do host. O cliente compara por entidade, sem adiantar o cursor global
TCP: UDP pode ultrapassar um SPAWN/DESPAWN que ainda chega pelo TCP.
Durante snapshot, descartar UDP; ao concluir, usar a revisao global dele como
baseline. UDP nao cria entidades desconhecidas nem ressuscita removidas.
Republicacao recupera estados que chegaram antes do SPAWN confiavel.

Clientes 0.4 sem `--udp` podem usar o host em modo legado TCP. Cada destinatario
recebe movimentos em apenas um canal. `MOVE` pelo TCP apos negociar UDP retorna
`ERROR USE_UDP` e nao modifica o mundo. Builds 0.3/anteriores sao recusadas.

## Limites e falhas

- Cliente: uma proposta a cada 34 ms, sem fila crescente de posicoes.
- Host: descarta Inputs alem de 60/s/conexao; mais de 400 registros de aplicacao
  DTLS/s/conexao encerram o participante, incluindo payload invalido.
- Leitura: ate 8 registros de aplicacao por chamada de polling.
- Saida: quota de ate `min(320, floor(1000/participantes))` estados/s por
  destinatario, um jogador por turno em round-robin; sem backlog. Frequencia
  efetiva depende do loop. Quotas fixas nao sao controle adaptativo de congestionamento.
- Handshake: timeout de 7 s; negociacao no cliente: 10 s. Sem estados validos
  do host por 10 s apos ativacao, o cliente falha sem fallback.
- Falha fatal encerra o participante; nao muda para UDP aberto ou movimentos
  TCP silenciosamente. WANT_READ/WANT_WRITE no envio de aplicacao tambem encerra:
  ainda falta fila de retry DTLS para backpressure. Handshake tem retries OpenSSL.
- Encerramento DTLS `close_notify` completo nao foi implementado. Fechar TCP
  invalida sessao e destroi socket/canal UDP; UDP sozinho nao mantem a sala.
- Sem validacao de velocidade, colisao, combate ou anticheat. Sem metricas,
  interpolacao, auditoria ou prova de resistencia a DoS/capacidade para gameplay.

Usa as mesmas DLLs OpenSSL 3 x64 do TLS, carregadas de pasta absoluta confiavel;
`--tls-library` vale para ambos os canais. Nao instalamos/baixamos DLLs.
Mantenha o runtime atualizado por sua origem confiavel, nao por sites de DLLs avulsas.

## Compilar e testar

```powershell
cmake -S . -B build-v04 -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-v04 --parallel 2
ctest --test-dir build-v04 --output-on-failure
```

`dtls_unit` usa proxy local para roundtrip cifrado, adulteracao, replay, perda,
reordenacao e chave errada. `udp_integration` exercita clientes nativos,
snapshot, clientes UDP/TCP mistos, quatro UDP, reconexao, requisito TLS, negociacao
duplicada, texto claro, timeout e queda do host. Testes de replica cobrem baseline
e ordenacao entre canais. Identidades dos testes sao temporarias.

Testes com sucesso apenas em loopback, nao LAN/Internet. Ainda faltam metricas,
carga/jitter prolongados, congestionamento adaptativo e interoperabilidade DTLS
com outro stack. Nao ha conclusao de aptidao para gameplay.
Para LAN, acrescentar `--lan --address IP_LOCAL_DO_HOST` aos comandos dos dois
lados; firewall pode bloquear as portas efemeras. Nenhuma regra foi criada.
Nao encaminhe portas nem exponha este laboratorio na Internet.

Referencias: [PSK callbacks](https://docs.openssl.org/3.0/man3/SSL_CTX_set_psk_client_callback/),
[TLS exporter](https://docs.openssl.org/3.0/man3/SSL_export_keying_material/),
[BIO datagram](https://docs.openssl.org/3.0/man3/BIO_s_datagram/).
