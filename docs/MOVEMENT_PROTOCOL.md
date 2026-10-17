# Protocolo de movimentos — contrato v1

## Status e limites desta entrega

O contrato v1 abaixo possui codec, validacao estrutural e controle de sequencia
testados. Na build 0.4, foi integrado com sockets UDP e DTLS opt-in (`--udp`),
incluindo negociacao, IDs e ciclo de vida. Veja o [guia da integracao](UDP_LAB.md).
O framing TCP segue versao 3, com build `dsr-network-lab-0.4`.
Ainda nao ha metricas, interpolacao ou sincronizacao com o DSR.

## Separacao dos canais

| Dados | Canal planejado | Regra |
|---|---|---|
| Senha, entrada, compatibilidade e autorizacao UDP | TCP/TLS | Concluir antes de aceitar movimentos |
| Roster, criacao e remocao de entidades | TCP/TLS | UDP nunca cria ou ressuscita uma entidade |
| Snapshot inicial e ressincronizacao | TCP/TLS | Aplicacao atomica; define baseline de revisao |
| Chat, mensagens privadas, erros e encerramento | TCP/TLS | Mantem os recursos existentes |
| Proposta de posicao do cliente | UDP protegido, tipo Input | Apenas sua propria entidade |
| Posicao aceita pelo host | UDP protegido, tipo State | Host distribui aos clientes, inclusive ao autor |
| Itens, dano, morte, bosses e progresso | Fora deste contrato | Exigem protocolo de eventos confiaveis e regras de autoridade proprias |

Nao enviar o mesmo fluxo continuo de movimentos simultaneamente por TCP e UDP.
Na ativacao, negociar explicitamente o modo. Se UDP nao puder ser
autenticado, informar a falha; nao aceitar datagramas em texto claro como fallback.
O modo legado TCP pode continuar existindo como modo separado e explicito.

Topologia: cliente -> host -> clientes. Convidados nao conversam entre si.
O host valida/aplica a proposta antes de publicar State, com sua propria revisao.
`Input` aqui significa proposta de posicao absoluta do laboratorio, NAO comando
de controle do DSR. Limites de coordenadas nao demonstram validacao de velocidade,
colisao, teleporte ou combate.

## Identidade e protecao

- `session`: 128 bits nao nulos, gerados pelo host com CSPRNG por abertura de sala.
  Pode usar os 16 bytes representados pelo ID hexadecimal atual da sala, sem
  copiar o texto ASCII. Nova sala implica novo ID.
- `connection`: 128 bits nao nulos, novos para cada entrada/reconexao, gerados
  pelo host com CSPRNG e comunicados exclusivamente no controle TLS autenticado.
  Nao e SteamID, nome, endereco IP, senha nem chave criptografica.
- `entity`: ID de jogador uint32 atribuido pelo host, nunca zero. Nao reutilizar
  IDs na mesma sessao; ao esgotar o espaco, encerrar a sessao, sem wrap.
- Input usa a conexao do remetente. State usa a conexao do destinatario;
  a entidade de State pode ser qualquer jogador ativo autorizado naquele mundo.
- Um State deve vir do canal autenticado do host. Um Input deve vir do canal
  autenticado do dono da entidade. IP/porta e IDs do payload nao comprovam isso.
- A integracao usa DTLS 1.2/OpenSSL, com PSK exclusiva derivada do TLS exporter
  e contexto session+connection. O handshake prova posse dessa chave; nunca
  confiar apenas nos IDs enviados. O fluxo concreto esta no [guia](UDP_LAB.md).
- A biblioteca deve tratar autenticidade, confidencialidade e protecao contra
  replay de registros. A sequencia da aplicacao complementa, nao substitui isso.
- Sem mudanca automatica de endpoint: revalidar por handshake autenticado.
  Ao perder/fechar o TCP, invalidar o canal UDP, seus vinculos e filas.

Os 68 bytes abaixo sao SOMENTE o payload da aplicacao, dentro do canal protegido.
Nao incluem cabecalhos IP/UDP, handshake, tag de autenticacao ou overhead da
biblioteca. O codec NAO cifra, autentica, gera identidades ou recebe da rede.

## Formato binario fixo

Um payload tem exatamente 68 bytes e uma mensagem. Inteiros multibyte usam
big-endian. Nao serializar structs com memcpy: padding e layout nao sao o contrato.

| Offset | Bytes | Campo | Valores |
|---|---|---|---|
| 0 | 4 | magic | ASCII `DSRM` |
| 4 | 1 | version | `1`, independente do protocolo TCP atual |
| 5 | 1 | kind | `1` Input; `2` State |
| 6 | 2 | flags | Zero |
| 8 | 2 | length | 68 (`0x0044`), sem overhead criptografico |
| 10 | 2 | reserved | Zero |
| 12 | 16 | session | Bytes opacos nao todos nulos |
| 28 | 16 | connection | Bytes opacos nao todos nulos |
| 44 | 8 | sequence | Uint64: 1 ate 2^64-2 |
| 52 | 4 | entity | Uint32 nao zero |
| 56 | 4 | x | Int32 em complemento de dois, escala 1000 |
| 60 | 4 | y | Mesmo formato |
| 64 | 4 | z | Mesmo formato |

Posicoes sao absolutas, nao deltas. Escala 1000 significa uma unidade do
laboratorio dividida em mil partes; NAO pressupoe metros ou unidades do DSR.
O codificador arredonda para o inteiro mais proximo, empates afastando de zero.
Erro de quantizacao de ate 0,0005 unidade por eixo. Normaliza zero negativo.
Valores originais e decodificados devem estar em [-100000, 100000], finitos.
Nao ha floats IEEE na rede, nomes, senha, orientacao, timestamp ou acks neste v1.
Novos campos/tipos exigem nova versao e negociacao; flags desconhecidas sao erro.

## Sequencia e descarte

Input: contador independente por conexao/entidade, iniciado em 1. Incrementar
por amostra emitida, mesmo quando a posicao permanece igual. O host atribui
a sua propria revisao ao estado aceito, sem copiar o contador do cliente.

State: `sequence` e a revisao autoritativa da entidade, correspondente ao
`PlayerState.revision` do host. Nao e um contador global de pacotes recebidos.
O cliente acompanha a ultima revisao POR ENTIDADE; um jogador nao bloqueia outro.
Reenviar o mesmo estado usa a mesma revisao; apenas uma nova atualizacao do
mundo incrementa a revisao. Um snapshot pode repetir posicoes sem nova revisao.

Para cada stream autorizado `(kind, session, connection, entity)`:

1. Autenticar o datagrama e verificar o vinculo ao canal autorizado.
2. Decodificar tamanho, magic, versao, campos reservados, IDs e coordenadas.
3. Confirmar direcao e entidade ativa/autorizada; rejeitar conexao ou sessao antiga.
4. Exigir `sequence > ultima_aceita`. Duplicado/atrasado e descartado sem aplicar.
5. Validar regras do mundo e limites de taxa; aplicar a atualizacao com sucesso.
6. Somente entao atualizar o cursor da sequencia. Falhas nao avancam o cursor.

Lacunas sao aceitas: apos 10, receber 13 aplica 13 sem esperar 11/12.
Nao retransmitir posicoes antigas e nao usar comparacao com wrap. Zero e
2^64-1 sao invalidos. Antes de esgotar contador/revisoes, interromper o stream
e renegociar contexto; para revisoes do mundo, abrir nova sessao. Nunca zerar
contador dentro da mesma identidade. Isto nao impede um cliente autorizado
malicioso de enviar saltos enormes: nao e uma solucao de anticheat.

`movement::Stream` implementa vinculo, elegibilidade e cursor, nao autorizacao
externa. `eligible()` nao altera estado; chamar `commit()` somente apos aplicacao
validada. A integracao serializa estas operacoes no event loop.
Um retorno falso de `eligible/commit` significa descartar. Excecoes do decoder
devem descartar o datagrama, sem derrubar o host nem responder ao remetente.

## Snapshot, entrada, saida e reconexao

- Enquanto recebe snapshot TCP, nao aplicar UDP. Ao concluir o snapshot,
  criar streams State com baseline da revisao global R desse snapshot.
  Todo movimento posterior ao snapshot tera revisao > R.
- Novas entidades ficam habilitadas apenas apos SPAWN confiavel. Criar seu
  cursor usando a revisao contida em SPAWN. Descartar UDP de entidade desconhecida.
- Nao guardar fila ilimitada de pacotes que chegaram antes de SPAWN/snapshot.
  Publicacao periodica de estado absoluto recupera a perda; precisa existir
  mesmo se o jogador parar. Assim nao dependemos de um unico pacote final.
- Em ressincronizacao, suspender UDP ate o novo snapshot atomico e reconstruir
  os cursores. O mundo pode descartar atualizacoes intermediarias deliberadamente.
- REMOVE confiavel elimina a entidade e o cursor. Datagramas atrasados nao
  podem recriar o jogador. Uma reconexao recebe nova connection e nova entidade.
- Ao cair o host, encerrar a sessao. Migracao de host nao faz parte deste marco.

## Limites propostos para a etapa de transporte

Os limites concretos implementados em 0.4 estao no [guia UDP](UDP_LAB.md).
O texto abaixo registra os objetivos do contrato, nao resultados de capacidade.

Iniciar o laboratorio com ate 30 propostas por segundo por cliente, agregando
atualizacoes pendentes pela mais recente. Isso e parametro experimental, nao
capacidade comprovada. O host precisa limitar entrada e saida agregadas, inclusive
por destinatario, e reduzir a taxa sob congestionamento; UDP nao elimina isso.
Estado absoluto precisa de republicacao periodica e fila limitada, nao backlog
de todos os movimentos. Estabelecer os limites concretos e medir o overhead
quando a biblioteca protegida for integrada. Nao ha fragmentacao de payload,
batching ou envio de snapshots grandes em UDP neste contrato.

RTT, perdas e idade dos estados serao instrumentados na fase de metricas;
nao comparar relogios absolutos de PCs diferentes nem inferir RTT so de sequence.
Interpolacao, predicao e reconciliacao ficam para uma etapa posterior.

## Verificacao disponivel agora

Arquivos: `protocol/movement_packet.*` (codec) e `protocol/movement_stream.*`
(sequencia/contexto). `tests/movement_unit.cpp` testa vetor binario de referencia,
roundtrip, quantizacao, limites, cabecalhos invalidos, truncamento, IDs, isolamento,
duplicacao, reordenacao, lacunas, snapshot, reconexao e proibicao de wrap.

Na raiz do projeto, apos recompilar:

```powershell
cmake --build build-v04 --parallel 2
ctest --test-dir build-v04 -R movement_unit --output-on-failure
```

Passar esses testes comprova regras locais; os testes de DTLS/UDP sao separados
no guia da integracao. Nao comprova auditoria criptografica, comportamento
LAN/Internet ou multiplayer do Dark Souls.
