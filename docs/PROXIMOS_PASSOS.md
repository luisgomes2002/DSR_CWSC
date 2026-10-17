# Próximos passos do DSR Network Lab

## Ponto de partida — versão 0.4

- Código separado em módulos, executável em `build-v04/dsr_net.exe`.
- Controle da sala, senha, chat e snapshots por TCP/TLS.
- Movimentos fictícios por UDP protegido com DTLS, ativado com `--udp`.
- IDs de sessão/conexão, sequências e descarte de posições antigas.
- Sete suítes automatizadas passaram, incluindo casos pontuais de perda,
  reordenação, adulteração, repetição, reconexão e quatro clientes UDP.
- O usuário confirmou funcionamento no teste manual local.

Isso ainda não é multiplayer do Dark Souls. Não houve integração com o jogo,
validação entre computadores, teste de Internet ou comprovação de capacidade
para gameplay. A compatibilidade entre instalações diferentes do DSR não foi
demonstrada. Não há garantia de risco zero de banimento em uma futura integração.

## 1. Métricas e movimentos automáticos — próximo trabalho

Status: pendente.

Implementar um gerador de trajetórias fictícias e coleta de métricas por conexão:

- RTT (ida e volta), distribuição dos tempos e variação do atraso.
- Datagramas enviados/recebidos, lacunas de sequência, duplicados e atrasados.
- Tráfego por segundo no cliente e no host, distinguindo payload de aplicação
  do que efetivamente for medido no transporte.
- Intervalos sem atualização, tamanho das filas e falhas do canal.
- Testes repetíveis com 2, 4 e depois 8 clientes, sem presumir que o jogo
  suportará essa quantidade.

Não comparar horários absolutos de PCs diferentes para inferir latência.
Sequências ausentes também podem representar reordenação ou descarte local;
definir a janela e o significado de cada contador antes de chamar isso de perda.

Entrega esperada: execução automática com relatório e parâmetros registrados,
usando o transporte atual como referência. Definir metas de aprovação a partir
das medições e dos requisitos de sincronização, sem inventar capacidade comprovada.

## 2. Condições adversas e robustez do transporte

Status: cobertura inicial existente; ampliação pendente.

- Simular atraso fixo e variável, perdas em rajadas, duplicação e reordenação.
- Limitar a velocidade e interromper a comunicação temporariamente.
- Fazer execuções prolongadas com vários clientes.
- Verificar consumo de memória, filas limitadas, recuperação e ausência de
  aplicação de posições antigas.
- Tratar backpressure de envio DTLS: hoje WANT_READ/WANT_WRITE no envio de
  aplicação encerra o canal; falta um tratamento de retry limitado e correto.
- Evoluir as quotas fixas para controle de tráfego adequado às medições e
  às condições de congestionamento, antes de considerar exposição na Internet.

Entrega esperada: cenários reproduzíveis e relatório comparável ao da fase 1.
As simulações devem afetar apenas o laboratório, não a rede inteira do usuário.

## 3. Validação entre computadores na rede local

Status: pendente.

- Um PC hospeda; outro conecta por IP, senha e pin do certificado.
- Conferir as dependências OpenSSL no segundo PC.
- Testar vários clientes, saída normal, queda de cliente, reconexão e queda do host.
- Observar bloqueios de firewall sem criar regras automaticamente.
- Repetir as métricas em rede real e comparar com loopback.

O desenho atual usa portas UDP efêmeras por participante, comunicadas no TLS.
Não pressupor que liberar apenas a porta TCP resolva a conectividade UDP.
Uma máquina virtual pode ajudar, mas deve ser registrada como esse tipo de teste.

Entrega esperada: roteiro de LAN validado e limitações documentadas.

## 4. Suavização da representação dos jogadores

Status: pendente.

- Criar uma representação visual mínima das entidades fictícias, se necessária
  para avaliar o movimento antes da integração com o jogo.
- Interpolar posições recebidas com um buffer limitado.
- Tratar pausas, mudanças bruscas e remoção/recriação de entidades.
- Medir o atraso adicional introduzido pela suavização.
- Separar estado autoritativo recebido da posição usada na apresentação.

Entrega esperada: movimentos visualmente avaliáveis sob os cenários anteriores.
Predição e reconciliação só devem ser acrescentadas quando houver modelo de
entradas/simulação que permita defini-las corretamente.

## 5. Conectividade pela Internet

Status: pendente; não expor o laboratório atual publicamente.

- Definir como localizar o host e atravessar NAT/CGNAT.
- Rever o desenho de portas e o vínculo de endpoint, hoje voltado a loopback/LAN.
- Avaliar opções de conectividade e seus custos/dependências com o usuário.
- Revisar autenticação, limites de recursos, comportamento sob abuso e manutenção
  das bibliotecas antes de liberar testes externos.

Requisito preservado: o mundo da sessão deve ser hospedado no PC do host.
Hospedar no PC não garante acesso direto através de qualquer NAT/CGNAT.
Não presumir autorização para contratar servidor, acrescentar um serviço externo,
usar relay, encaminhar portas ou alterar firewall. Se necessário, apresentar
as opções e obter uma decisão antes de mudar esses requisitos.

Entrega esperada: estratégia escolhida explicitamente e teste externo controlado.

## 6. Integração mínima e isolamento do Dark Souls Remastered

Status: pendente; exige um marco próprio, separado da rede.

- Identificar e comparar as versões dos executáveis que serão suportadas;
  a versão informada pelo usuário foi app 1.03.1 / regulation 1.04.
- Começar com identificação e leitura controlada de dados do jogador,
  sem escrita em memória ou saves.
- Validar isolamento dos saves e bloqueio dos serviços oficiais antes de
  experimentar alterações no jogo. Não tratar as investigações anteriores
  como prova de que essas proteções já funcionam.
- Usar backups e dados de teste, com autorização explícita antes de executar
  ferramentas que carreguem código ou alterem o processo do jogo.
- Confirmar separadamente a compatibilidade de cada instalação suportada;
  o laboratório sem Steam não comprova, por si só, compatibilidade entre elas.

Entrega esperada: camada mínima de leitura validada e evidências do isolamento
antes de avançar para modificações em execução. Não prometer ausência de ban.

## 7. Sincronização de gameplay por etapas

Status: pendente; depende das validações de rede e integração.

Ordem inicial sugerida, sujeita ao que a investigação do jogo permitir:

1. Representação de jogador remoto e movimento.
2. Orientação, animações e equipamentos.
3. Ações de combate, dano e morte.
4. Inimigos, bosses e autoridade sobre o mundo.
5. Itens, eventos, progresso e transições de área.
6. Recuperação de inconsistências, entrada tardia e testes prolongados.

Definir confiabilidade, ordem e prevenção de aplicação duplicada para cada
evento. Não transportar itens, dano ou progresso como se fossem posições
descartáveis. O host precisa validar as alterações relevantes do mundo.

Entrega esperada: cada recurso demonstrado e testado isoladamente antes de
ser considerado parte de uma experiência cooperativa jogável. Sem invasões
como objetivo do projeto.

## Documentação relacionada

- [Teste atual do canal UDP protegido](UDP_LAB.md)
- [Contrato do protocolo de movimentos](MOVEMENT_PROTOCOL.md)
- [Marco anterior de TLS](TLS_LAB.md)
- [Parecer anterior de viabilidade](../../dsr-analysis/PARECER_VIABILIDADE.md)

Atualizar este documento a cada marco, distinguindo implementado, testado e
ainda proposto. Esta lista não autoriza antecipadamente executar fases futuras.
