# Roteiro de apresentação — Relógio e alarme com FreeRTOS

13 slides principais para cerca de 12 minutos de exposição, além da demonstração. Os slides 14–16 são apoio para a arguição. O mesmo roteiro está nas notas do apresentador do PowerPoint.

## Sequência de fala

### 01. Relógio e alarme com FreeRTOS
**Tempo sugerido:** 30 s.

Apresentar o produto como um relógio autônomo configurado pelo joystick. Explicar que o computador fornece somente alimentação durante a operação. Anunciar as três decisões que orientam a apresentação: separar tarefas, concentrar o estado no controlador e usar Timer1 para a contagem do calendário. A foto mostra o protótipo utilizado na apresentação.

**Ponto para a arguição:** Não confundir operação autônoma com referência de tempo de alta exatidão.

### 02. Requisitos e produto
**Tempo sugerido:** 40 s.

Relacionar cada função ao enunciado do professor. Mostrar no protótipo o LCD, o joystick e o botão D5. A alimentação USB não fornece data e hora ao programa. O potenciômetro pertence ao contraste do LCD, não ao volume. O firmware permite ajustar segundos para acertar o horário pela interface. A operação não exige terminal nem aplicação no computador.

**Ponto para a arguição:** A0 e A1 são constantes numéricas equivalentes a 54 e 55 no Mega; não são strings.

### 03. RTOS, bare metal e máquina de estados
**Tempo sugerido:** 55 s.

Explicar que uma solução bare metal bem projetada também poderia atender ao problema. O RTOS foi solicitado no exame e permite atribuir responsabilidades e prioridades explícitas. Quando o alarme fica pronto, ele pode interromper a apresentação no LCD. A máquina de estados continua presente dentro do controlador para decidir o significado de cada evento. O RTOS não aumenta a frequência da CPU nem melhora sozinho a precisão do oscilador.

**Ponto para a arguição:** Preempção é a retirada da CPU de uma tarefa pronta de menor prioridade quando outra de maior prioridade deve executar.

### 04. Arquitetura por eventos
**Tempo sugerido:** 60 s.

Percorrer as setas da esquerda para a direita. A tarefa de botões produz um evento, como Selecionar ou Soneca. Somente a tarefa de relógio decide como esse evento muda o estado. Ela envia ao alarme um booleano e ao display uma cópia consistente do estado. A ISR incrementa um contador; o controlador o lê atomicamente. O termo dono único se refere ao estado civil, não à ausência de toda variável global no programa. Os handles das filas e o contador da ISR continuam existindo.

**Ponto para a arguição:** Copiar uma estrutura pela fila só isola os dados se ela não depender de ponteiros para objetos mutáveis externos; o snapshot deste projeto contém dados por valor.

### 05. Tarefas e prioridades
**Tempo sugerido:** 55 s.

Justificar a prioridade 3 do alarme pela resposta à ativação e ao desligamento. Explicar que ele fica bloqueado quando inativo e aguarda a próxima fase quando está tocando. Relógio e botões têm prioridade 2; o port habilita divisão de tempo entre tarefas prontas de mesma prioridade. O display tem prioridade 1 e pode ser preemptado. A prioridade não é uma porcentagem reservada de CPU. A espera em fila entrega a CPU a outra tarefa; ela não é busy waiting.

**Ponto para a arguição:** O display não retém um mutex de aplicação que a tarefa de relógio precise adquirir.

### 06. Mutex versus filas
**Tempo sugerido:** 65 s.

Explicar que o mutex é adequado quando há um recurso realmente compartilhado. O problema não é a existência do mutex, mas a duração e a disciplina da exclusividade. A herança de prioridade reduz a interferência de tarefas intermediárias quando alguém de maior prioridade espera pelo recurso. Nesta arquitetura, o display recebe uma cópia e não segura um lock necessário ao controle. A fila de entrada preserva ordem; as caixas de saída substituem dados antigos porque o consumidor precisa do estado atual.

**Ponto para a arguição:** Uma fila não garante ausência de toda condição de corrida. A segurança aqui depende de dono único, cópia de dados, atomicidade na ISR e política explícita para fila cheia.

### 07. Domínios temporais
**Tempo sugerido:** 65 s.

Distinguir o timer do escalonador da referência usada para avançar o calendário. O watchdog permanece no tick do FreeRTOS. Timer1 utiliza o clock principal da placa em CTC, a 100 Hz nominal. Timer0 mantém millis e micros do Arduino e aparece no diagnóstico. Timer2 é usado por tone para o buzzer. O firmware não usa Timer0 como um RTC de calendário. Trocar para uma biblioteca de RTC no Timer2 exigiria tratar o conflito com tone.

**Ponto para a arguição:** pdMS_TO_TICKS(20) produz um tick nesta configuração de 62 Hz; isso não transforma o período físico em exatamente 20 ms.

### 08. Timer1 e atomicidade
**Tempo sugerido:** 65 s.

Apresentar a fórmula do CTC e apontar que todos os valores são nominais. A ISR faz apenas o incremento, mantendo baixo o trabalho em interrupção. No AVR, a leitura de quatro bytes pode ser interrompida entre bytes; volatile não resolve isso. ATOMIC_RESTORESTATE protege somente a cópia e restaura o estado anterior. O controlador usa a subtração unsigned para recuperar as contagens desde a amostra anterior, inclusive atravessando rollover. Um atraso no agendamento não precisa se tornar perda de segundos.

**Ponto para a arguição:** Esse mecanismo recupera contagens registradas. Não reconstrói interrupções perdidas se o hardware deixar de distingui-las durante um bloqueio excessivo.

### 09. Entrada e edição
**Tempo sugerido:** 55 s.

Mostrar a foto da edição de segundos e a do horário do alarme. A tarefa de botões filtra a entrada e gera eventos sem decidir a regra do menu. O rascunho fica separado do relógio corrente. Salvar apenas o alarme mantém a hora que continuou avançando; salvar uma alteração da hora aplica o valor escolhido e reinicia a fração civil. O clique curto de SEL é emitido na soltura e o longo não gera um curto adicional.

**Ponto para a arguição:** Os 50 ms de debounce são estabilidade observada na base Timer1, não apenas um delay depois de qualquer leitura.

### 10. Alarme e soneca
**Tempo sugerido:** 65 s.

Percorrer os estados. No segundo zero do horário habilitado, o controlador inicia o toque. D5 ou SEL curto inicia a mesma soneca configurada. A contagem é relativa, por isso a meia-noite não quebra a comparação. O exemplo de 60 segundos usa a configuração mínima de um minuto; o padrão é cinco minutos. Cada toque termina em até 60 segundos. A repetição da soneca requer novo acionamento; ela não se repete automaticamente para sempre.

**Ponto para a arguição:** D5 durante a soneca cancela e desabilita o alarme. SEL longo encerra o aviso, mas preserva a habilitação diária. A chave de ocorrência evita redisparo no mesmo minuto e data.

### 11. Precisão e exatidão
**Tempo sugerido:** 60 s.

Explicar que prioridade organiza resposta da CPU; ela não calibra o oscilador. A diferença observada na aferição para apresentação não determina sozinha a deriva. Para medir deriva, compara-se a mudança do erro em um intervalo conhecido. Timer1 usa o oscilador principal e pode adiantar ou atrasar. Uma melhoria de referência poderia usar RTC externo ou Timer2 assíncrono com cristal apropriado, mas não faz parte desta montagem. Não apresentar essa alternativa como algo implementado.

**Ponto para a arguição:** Timer2 assíncrono não é a única solução e também tem tolerância. A montagem atual não dispõe de sincronização automática com uma fonte independente de horário.

### 12. Resultados e verificação
**Tempo sugerido:** 55 s.

Apresentar a evidência adequada para cada conclusão. As fotos mostram a montagem e a interface; a compilação confirma a construção para ATmega2560; os testes verificam a lógica extraída do próprio sketch. O teste de seis horas trabalha com 2.160.000 contagens nominais, não é uma certificação de deriva física de seis horas. O consumo estático de 729 bytes não inclui todas as alocações do kernel. A integração mantém verificações de criação de recursos e diagnóstico de margem de pilha.

**Ponto para a arguição:** Não transformar o número de bytes estáticos em uma afirmação de memória total livre; há pilhas, filas, TCBs e outras estruturas em execução.

### 13. Conclusão e demonstração
**Tempo sugerido:** 40 s + demonstração.

Concluir com os três pontos centrais: um dono para o estado, comunicação por mensagens e separação das bases de tempo. Na bancada, abrir a edição, navegar até o horário do alarme, configurá-lo para o próximo minuto e salvar. Habilitar pelo D5 na tela normal. Enquanto aguarda, mostrar que o relógio continua avançando. No disparo, pressionar D5 para exibir a soneca. Encerrar com SEL longo, ou explicar que D5 durante a soneca também desabilita o alarme. Não reinicializar a placa durante a demonstração, pois a configuração é volátil.

**Ponto para a arguição:** Para uma demonstração curta, configurar soneca de um minuto. A exposição não precisa esperar todo esse minuto: pode mostrar a contagem e encerrar o aviso.

### 14. Apoio: registradores
**Tempo sugerido:** Consultar se solicitado.

Explicar a ordem de inicialização: parar o clock, desabilitar interrupção do periférico, limpar controle e contador, configurar comparação, limpar flag, habilitar interrupção e ligar o timer. A sequência ocorre em bloco atômico antes das tarefas. Para prescaler 1024 e OCR1A 15624, a divisão nominal seria 1 Hz, mas a unidade de todos os contadores teria de ser revista. Como a fonte de clock continuaria a mesma, a tolerância física também permaneceria.

**Ponto para a arguição:** F_CPU é uma constante de compilação, não uma medição automática da frequência real da placa.

### 15. Apoio: perguntas da banca
**Tempo sugerido:** Consultar se solicitado.

Esclarecer que ausência de mutex compartilhado remove aquele caminho de inversão de prioridade, sem prometer latência nula ou impossibilidade de qualquer defeito. As filas possuem proteção interna e capacidade finita. O snapshot de saída pode substituir outro porque interessa o estado atual. A notificação contadora registra evento de entrada descartado. O high-water mark informa a menor margem de pilha observada no percurso executado, não uma prova de todos os percursos possíveis.

**Ponto para a arguição:** StackType_t é uint8_t no port AVR usado: profundidade 512 corresponde a 512 bytes. Alarme 256 + relógio 512 + botões 288 + display 512 = 1.568 bytes.

### 16. Referências
**Tempo sugerido:** Consultar se solicitado.

O repositório reúne o relatório, o firmware consolidado, as fotos originais e as versões anteriores. A pasta referencias preserva os excertos reais do FreeRTOS e a origem dos trechos. A apresentação usa o mesmo conteúdo técnico da entrega e mantém a distinção entre comportamento do software, observação do produto e precisão da referência física.

**Ponto para a arguição:** O relatório possui as referências completas e a comparação entre a versão com mutex e a versão por eventos.

## Preparação da demonstração

- Manter a placa ligada: a configuração permanece em SRAM e volta ao estado inicial após reinicialização.
- Confirmar que a hora exibida está acertada e o contraste permite leitura pela banca.
- Configurar o alarme para o próximo minuto e salvar; habilitar por D5 na tela normal.
- Durante o toque, D5 ou SEL curto inicia a soneca. SEL longo encerra o aviso.
- Durante a soneca, D5 cancela e também desabilita o alarme; explicar essa diferença.
- Usar o computador somente como alimentação durante a demonstração. O monitor serial não integra a operação.
- Manter a apresentação e o PDF disponíveis localmente.

## Respostas que exigem precisão

**Por que não houve sincronização automática?** A montagem não dispõe de uma fonte independente de horário acessível ao firmware. Timer1 mede intervalos do próprio oscilador; não determina sozinho se a hora está correta. Uma nova referência exigiria integração adicional.

**Por que vTaskDelayUntil e não apenas vTaskDelay?** O primeiro mantém a referência periódica a partir do despertar planejado anterior; o segundo espera relativamente ao instante da chamada. O calendário desta aplicação depende do contador Timer1, não da quantidade de despertares.

**Como funciona o rollover?** A diferença unsigned calcula as contagens entre amostras, desde que não transcorra um ciclo completo do contador entre elas. A 100 Hz, 32 bits dão aproximadamente 497 dias. Esse contador é distinto do tick de 16 bits do kernel.

**Pode chamar o sistema de imune à inversão de prioridade?** A formulação precisa é: o display não retém mutex de aplicação necessário ao relógio ou ao alarme. Isso remove esse caminho de inversão. Ainda existem escalonamento, seções internas do kernel e latência de interrupção.

**O que a aferição para apresentação demonstra?** Uma comparação pontual do horário. A taxa de deriva exige comparar a mudança do erro durante um intervalo conhecido.

**A soneca repete para sempre?** Não há repetição automática infinita. Cada novo adiamento depende de D5 ou SEL enquanto o alarme toca. O toque tem duração máxima de 60 segundos.

**Existe proteção contra falta de memória?** A criação de cada recurso tem o retorno verificado. Os recursos são criados na inicialização, sem alocação repetitiva nos ciclos normais. A biblioteca habilita verificação de overflow e o diagnóstico acompanha a margem de pilha.
