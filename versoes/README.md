# Evolução do firmware

A entrega atual está em [`firmware/RelogioFinal`](../firmware/RelogioFinal). Esta pasta preserva as etapas anteriores para comparação do projeto.

| Pasta | Organização | Base de tempo |
| --- | --- | --- |
| `01_mutex_inicial` | Estado compartilhado protegido por mutex | Tick do FreeRTOS |
| `02_mutex_menu` | Mutex e revisão da interface | Tick do FreeRTOS |
| `03_filas_watchdog` | Eventos, filas e estado centralizado | Tick do FreeRTOS |
| `04_timer1_ctc` | Filas com temporização dedicada | Timer1 CTC, 100 Hz |
| `05_timer1_contador_livre` | Revisão da leitura do contador | Timer1 livre, prescaler 1024 |
| `06_eventos_modular` | Reconstrução por eventos, quatro tarefas, três filas | Timer1 CTC, 100 Hz |

A versão final consolida o código de `06_eventos_modular` em um `.ino` e um cabeçalho. Os corpos das funções e a configuração inicial de horário foram preservados. A mudança desta entrega é a organização dos arquivos e a documentação.

O histórico mostra duas decisões independentes: a comunicação evoluiu de exclusividade sobre dados compartilhados para troca de mensagens; a contagem do calendário passou a utilizar um temporizador dedicado. Uma fila não corrige a frequência do oscilador, e um temporizador não substitui a proteção de acesso ao estado.
