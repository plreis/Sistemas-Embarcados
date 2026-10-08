#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <LiquidCrystal.h>
#include <string.h>
#include "relogio_tipos.h"
#include "base_tempo.h"

/*
 * Mega 2560 / LCD 1602 paralelo.
 * TaskRelogio e a unica dona do estado. As demais tarefas usam filas.
 * Potenciometro: somente contraste V0 do LCD, sem controle de volume.
 */

// Pinagem preservada da montagem.
constexpr uint8_t LCD_RS = 22, LCD_E = 23;
constexpr uint8_t LCD_D4 = 24, LCD_D5 = 25, LCD_D6 = 26, LCD_D7 = 27;
constexpr uint8_t JOY_X = 54, JOY_Y = 55; // A0 e A1 no Mega; nao sao strings.
constexpr uint8_t JOY_SEL = 2, BOTAO_SONECA = 5, BUZZER = 8, LED_ALARME = 9;
constexpr uint8_t SONECA_MIN = 1, SONECA_MAX = 10;
constexpr uint8_t NUM_CAMPOS = 9, TAM_FILA_EVENTOS = 16;
constexpr uint32_t AVISO_FILA_CHEIA = 1UL;
// LCD paralelo de 16 colunas e 2 linhas.

LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Filas de eventos e estados.
QueueHandle_t filaEventos, filaAlarme, filaTela;
// Handles usados no diagnostico de pilhas.
TaskHandle_t tarefaRelogio, tarefaBotoes, tarefaAlarme, tarefaDisplay;

/* Todas as conversoes ms -> ticks usam a API oficial. Nunca atrasar zero ticks. */
TickType_t ticksMs(uint32_t ms) {
  TickType_t ticks = pdMS_TO_TICKS(ms);
  return ticks == 0 ? 1 : ticks;
}

/* A base de tempo esta isolada em base_tempo.cpp. O watchdog continua
 * exclusivo do escalonador; Timer2 continua disponivel para tone(). */
#ifndef portUSE_WDTO
#error "Esta revisao requer o tick FreeRTOS no watchdog."
#endif

uint32_t contagensSegundos(uint16_t segundos) {
  return CONTAGENS_POR_SEGUNDO * segundos;
}

// Calendario gregoriano na faixa de configuracao 2000..2099.

uint8_t diasNoMes(uint8_t mes, uint16_t ano) {
  if (mes == 2)
    return ((ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0) ? 29 : 28;
  return (mes == 4 || mes == 6 || mes == 9 || mes == 11) ? 30 : 31;
}
// Limita o dia ao mudar mes ou ano.

void normalizarDia(Configuracao &c) {
  uint8_t limite = diasNoMes(c.mes, c.ano);
  if (c.dia > limite) c.dia = limite;
}

// Incremento/decremento circular de um campo.
uint8_t circular(uint8_t valor, int8_t delta, uint8_t minimo, uint8_t maximo) {
  if (delta > 0) return valor == maximo ? minimo : valor + 1;
  return valor == minimo ? maximo : valor - 1;
}

// CONTAGEM DOS SEGUNDOS

void avancarSegundo(EstadoRelogio &r) {
  Configuracao &c = r.cfg;
  if (++r.segundo < 60) return;
  r.segundo = 0;
  if (++c.minuto < 60) return;
  c.minuto = 0;
  if (++c.hora < 24) return;
  c.hora = 0;
  if (++c.dia <= diasNoMes(c.mes, c.ano)) return;
  c.dia = 1;
  if (++c.mes <= 12) return;
  c.mes = 1;
  c.ano = c.ano == 2099 ? 2000 : c.ano + 1;
}

uint32_t chaveMinuto(const Configuracao &c) {
  /* Chave distinta por data/minuto na faixa 2000..2099; zero fica reservado. */
  uint32_t dia = (uint32_t)(c.ano - 2000) * 372UL +
                 (uint32_t)(c.mes - 1) * 31UL + c.dia - 1;
  return dia * 1440UL + (uint16_t)c.hora * 60U + c.minuto + 1UL;
}


// Uma ocorrencia por data/minuto; nao redispara apos silenciar.
void avaliarAlarme(EstadoRelogio &r) {
  if (!r.alarmeLigado || r.cfg.hora != r.cfg.alarmeHora ||
      r.cfg.minuto != r.cfg.alarmeMinuto) return;
  uint32_t chave = chaveMinuto(r.cfg);
  if (chave == r.ultimoDisparo) return;
  r.ultimoDisparo = chave;
  /* Se ja ha uma soneca/toque, a ocorrencia coincidente nao cria outro aviso. */
  if (r.sonecaContagens == 0 && r.toqueContagens == 0)
    r.toqueContagens = contagensSegundos(60);
}

/* Unica implementacao de soneca para D5 e clique curto no joystick. */
void iniciarSoneca(EstadoRelogio &r) {
  if (!r.alarmeLigado || r.toqueContagens == 0) return;
  r.toqueContagens = 0;
  r.sonecaContagens = contagensSegundos((uint16_t)r.cfg.sonecaMinutos * 60U);
}

void descontarPrazo(uint32_t &prazo, uint32_t decorrido) {
  prazo = decorrido >= prazo ? 0 : prazo - decorrido;
}

void atualizarPrazos(EstadoRelogio &r, uint32_t decorrido) {
  descontarPrazo(r.avisoContagens, decorrido);
  if (r.sonecaContagens != 0) {
    if ((uint32_t)decorrido >= r.sonecaContagens) {
      uint32_t excedente = (uint32_t)decorrido - r.sonecaContagens;
      r.sonecaContagens = 0;
      r.toqueContagens = contagensSegundos(60);
      descontarPrazo(r.toqueContagens, excedente);
    } else r.sonecaContagens -= decorrido;
  } else descontarPrazo(r.toqueContagens, decorrido);
}

/* Percorre as fronteiras de segundo na ordem em que ocorreram.
 * Se a tarefa retomar apos um atraso, o toque iniciado durante esse intervalo
 * tambem envelhece. Nao transforma um alarme ja vencido em 60 s novos. */
void consumirTempo(EstadoRelogio &r, uint32_t &fracao, uint32_t decorrido) {
  while (decorrido != 0) {
    uint32_t ateSegundo = CONTAGENS_POR_SEGUNDO - fracao;
    uint32_t passo = decorrido < ateSegundo ? decorrido : ateSegundo;
    atualizarPrazos(r, passo);
    fracao += passo;
    decorrido -= passo;
    if (fracao == CONTAGENS_POR_SEGUNDO) {
      fracao = 0;
      avancarSegundo(r);
      avaliarAlarme(r);
    }
  }
}

void alterarCampo(Edicao &e, int8_t delta) {
  Configuracao &c = e.rascunho;
  if (e.campo <= 2) e.mudouHora = true;
  else if (e.campo <= 5) e.mudouData = true;
  switch (e.campo) {
    case 0: c.hora = circular(c.hora, delta, 0, 23); break;
    case 1: c.minuto = circular(c.minuto, delta, 0, 59); break;
    case 2: e.segundoRascunho = circular(e.segundoRascunho, delta, 0, 59); break;
    case 3: c.dia = circular(c.dia, delta, 1, diasNoMes(c.mes, c.ano)); break;
    case 4: c.mes = circular(c.mes, delta, 1, 12); normalizarDia(c); break;
    case 5:
      c.ano = delta > 0 ? (c.ano == 2099 ? 2000 : c.ano + 1)
                       : (c.ano == 2000 ? 2099 : c.ano - 1);
      normalizarDia(c);
      break;
    case 6: c.alarmeHora = circular(c.alarmeHora, delta, 0, 23); break;
    case 7: c.alarmeMinuto = circular(c.alarmeMinuto, delta, 0, 59); break;
    case 8: c.sonecaMinutos = circular(c.sonecaMinutos, delta, SONECA_MIN, SONECA_MAX); break;
  }
}

void salvarEdicao(EstadoRelogio &r, Edicao &e, uint32_t &fracaoSegundo) {
  /* Editar so alarme/soneca nao retrocede a hora que continuou contando. */
  if (e.mudouHora) {
    r.cfg.hora = e.rascunho.hora;
    r.cfg.minuto = e.rascunho.minuto;
    r.segundo = e.segundoRascunho;
    fracaoSegundo = 0; // Nova fase civil; nao reinicia o contador fisico.
    ++r.ajustesHora; // Distingue ajuste manual de deriva no diagnostico.
  }
  if (e.mudouData) {
    r.cfg.dia = e.rascunho.dia;
    r.cfg.mes = e.rascunho.mes;
    r.cfg.ano = e.rascunho.ano;
  }
  r.cfg.alarmeHora = e.rascunho.alarmeHora;
  r.cfg.alarmeMinuto = e.rascunho.alarmeMinuto;
  r.cfg.sonecaMinutos = e.rascunho.sonecaMinutos;
  e.ativa = false;
}

void tratarEvento(Evento evento, EstadoRelogio &r, Edicao &e, uint32_t &fracao) {
  if (evento == Evento::SEL_LONGO) {
    r.toqueContagens = 0;
    r.sonecaContagens = 0;
    e.ativa = false; // Cancela o rascunho; mantem o alarme para a proxima ocorrencia.
    return;
  }
  if (r.toqueContagens != 0) {
    if (evento == Evento::SONECA || evento == Evento::SEL_CURTO) iniciarSoneca(r);
    return;
  }
  if (r.sonecaContagens != 0) {
    if (evento == Evento::SONECA) {
      r.sonecaContagens = 0;
      r.alarmeLigado = false;
    }
    return;
  }
  if (evento == Evento::SONECA) {
    if (e.ativa) e.ativa = false; // D5 cancela a edicao.
    else r.alarmeLigado = !r.alarmeLigado;
    return;
  }
  if (evento == Evento::SEL_CURTO) {
    if (e.ativa) salvarEdicao(r, e, fracao);
    else {
      e.rascunho = r.cfg;
      e.segundoRascunho = r.segundo;
      e.ativa = true;
      e.mudouHora = e.mudouData = false;
      e.campo = 0;
    }
    return;
  }
  if (!e.ativa) return;
  switch (evento) {
    case Evento::DIREITA: e.campo = (e.campo + 1) % NUM_CAMPOS; break;
    case Evento::ESQUERDA: e.campo = (e.campo + NUM_CAMPOS - 1) % NUM_CAMPOS; break;
    case Evento::CIMA: alterarCampo(e, 1); break;
    case Evento::BAIXO: alterarCampo(e, -1); break;
    default: break;
  }
}

/*
 * 0 = nenhum evento; 1 = curto; 2 = longo.
 * Debounce por estado e borda, sem esperar o usuario soltar o botao.
 * SEL curto e emitido na soltura; D5 e emitido no aperto confirmado.
 */
uint8_t lerBotao(uint8_t pino, BotaoDebounce &b, bool permiteLongo, TickType_t agora) {
  bool leitura = digitalRead(pino) == LOW;
  if (leitura != b.leituraAnterior) {
    b.leituraAnterior = leitura;
    b.mudouEm = agora;
  }
  if (leitura != b.estavel &&
      (TickType_t)(agora - b.mudouEm) >= ticksMs(50)) {
    b.estavel = leitura;
    if (leitura) {
      b.pressionouEm = agora;
      b.longoEnviado = false;
      if (!permiteLongo) return 1;
    } else if (permiteLongo && !b.longoEnviado) return 1;
  }
  if (permiteLongo && b.estavel && leitura && !b.longoEnviado &&
      (TickType_t)(agora - b.pressionouEm) >= ticksMs(1000)) {
    b.longoEnviado = true;
    return 2;
  }
  return 0;
}

int8_t direcaoEixo(int leitura) {
  return leitura < 200 ? -1 : leitura > 800 ? 1 : 0;
}

int8_t lerEixo(int8_t direcao, EixoJoystick &e, TickType_t agora) {
  if (direcao == 0) { e.anterior = 0; return 0; }
  if (direcao != e.anterior ||
      (TickType_t)(agora - e.repetiuEm) >= ticksMs(350)) {
    e.anterior = direcao;
    e.repetiuEm = agora;
    return direcao;
  }
  return 0;
}

void enviarEvento(Evento evento) {
  if (xQueueSend(filaEventos, &evento, 0) != pdPASS) {
    /* Sobrecarga nao trava a leitura nem passa despercebida ao usuario. */
    xTaskNotify(tarefaRelogio, AVISO_FILA_CHEIA, eSetBits);
  }
}

void TaskBotoes(void *parametros) {
  (void)parametros;
  BotaoDebounce sel = {}, d5 = {};
  EixoJoystick eixoX = {}, eixoY = {};
  TickType_t ultima = xTaskGetTickCount();
  for (;;) {
    TickType_t agora = xTaskGetTickCount();
    uint8_t eventoSel = lerBotao(JOY_SEL, sel, true, agora);
    if (eventoSel) enviarEvento(eventoSel == 2 ? Evento::SEL_LONGO : Evento::SEL_CURTO);
    if (lerBotao(BOTAO_SONECA, d5, false, agora)) enviarEvento(Evento::SONECA);

    int8_t x = direcaoEixo(analogRead(JOY_X));
    int8_t y = direcaoEixo(analogRead(JOY_Y));
    int8_t dx = lerEixo(x, eixoX, agora);
    /* Prioriza navegacao nas diagonais: evita alterar outro campo por acidente. */
    int8_t dy = lerEixo(x == 0 ? y : 0, eixoY, agora);
    if (dx) enviarEvento(dx > 0 ? Evento::DIREITA : Evento::ESQUERDA);
    else if (dy) enviarEvento(dy > 0 ? Evento::CIMA : Evento::BAIXO);
    vTaskDelayUntil(&ultima, ticksMs(20));

  }
}

void publicarTela(const EstadoRelogio &r, const Edicao &e,
                  uint32_t referencia, uint32_t maiorIntervalo) {
  Tela tela = {};
  tela.contagem = referencia;
  tela.millisAmostra = millis();
  tela.maiorIntervalo = maiorIntervalo;
  tela.ajustesHora = r.ajustesHora;
  tela.timerOk = baseTempoIntegra();
  tela.horaReal = r.cfg.hora;
  tela.minutoReal = r.cfg.minuto;
  tela.segundoReal = r.segundo;
  tela.cfg = e.ativa ? e.rascunho : r.cfg;
  tela.segundo = e.ativa ? e.segundoRascunho : r.segundo;
  tela.campo = e.campo;
  tela.alarmeLigado = r.alarmeLigado;
  tela.editando = e.ativa;
  tela.tocando = r.toqueContagens != 0;
  tela.emSoneca = r.sonecaContagens != 0;
  tela.avisoEntrada = r.avisoContagens != 0;
  uint32_t segundo = CONTAGENS_POR_SEGUNDO;
  tela.sonecaSegundos = (uint16_t)((r.sonecaContagens + segundo - 1) / segundo);
  /* Mailbox de UMA posicao: o LCD sempre recebe o estado mais recente. */
  xQueueOverwrite(filaTela, &tela);
}

void TaskRelogio(void *parametros) {
  (void)parametros;
  EstadoRelogio r = {};
  r.cfg = {18, 15, 7, 10, 2026, 17, 50, 5};
  r.segundo = 0; // Segundos iniciais explicitos; sincronizar no menu SEG.
  r.alarmeLigado = true;
  Edicao edicao = {};
  iniciarBaseTempo();
  uint32_t anterior = lerBaseTempo();
  uint32_t anteriorCiclo = anterior;
  TickType_t ultima = xTaskGetTickCount(), ultimaTela = ultima;
  uint32_t fracaoSegundo = 0;
  bool alarmeAnterior = false;
  uint32_t maiorIntervalo = 0;
  publicarTela(r, edicao, anterior, maiorIntervalo);

  for (;;) {
    TickType_t agora = xTaskGetTickCount();
    // Contador de hardware estendido; diferenca unsigned atravessa o rollover.
    uint32_t referencia = lerBaseTempo();
    uint32_t decorrido = referencia - anterior;
    anterior = referencia;
    uint32_t intervaloCiclo = referencia - anteriorCiclo;
    anteriorCiclo = referencia;
    if (intervaloCiclo > maiorIntervalo) maiorIntervalo = intervaloCiclo;
    consumirTempo(r, fracaoSegundo, decorrido);

    uint32_t avisos = 0;
    if (xTaskNotifyWait(0, AVISO_FILA_CHEIA, &avisos, 0) == pdTRUE &&
        (avisos & AVISO_FILA_CHEIA)) r.avisoContagens = contagensSegundos(2);

    Evento evento;
    /* Limite por ciclo: uma rajada de entradas nao monopoliza o controlador. */
    for (uint8_t i = 0; i < TAM_FILA_EVENTOS; ++i) {
      if (xQueueReceive(filaEventos, &evento, 0) != pdPASS) break;
      // Data o evento no instante do consumo, sem descartar tempo da soneca.
      uint32_t instanteEvento = lerBaseTempo();
      consumirTempo(r, fracaoSegundo, instanteEvento - anterior);
      anterior = instanteEvento;
      tratarEvento(evento, r, edicao, fracaoSegundo);
    }
    avaliarAlarme(r);
    bool tocar = r.toqueContagens != 0;
    if (tocar != alarmeAnterior) {
      xQueueOverwrite(filaAlarme, &tocar);
      alarmeAnterior = tocar;
    }
    if ((TickType_t)(agora - ultimaTela) >= ticksMs(100)) {
      publicarTela(r, edicao, anterior, maiorIntervalo);
      ultimaTela = agora;
    }
    /* Agenda absoluta: tempo de processamento nao e somado ao periodo. */
    vTaskDelayUntil(&ultima, ticksMs(20));
  }
}

void TaskAlarme(void *parametros) {
  (void)parametros;
  bool ativo = false, fase = false;
  for (;;) {
    bool comando;
    /* Desligado dorme na fila; ligado acorda por comando ou fim do pulso. */
    if (xQueueReceive(filaAlarme, &comando, ativo ? ticksMs(200) : portMAX_DELAY) == pdPASS) {
      ativo = comando;
      fase = ativo;
    } else fase = !fase;
    if (ativo && fase) tone(BUZZER, 2000);
    else noTone(BUZZER);
    digitalWrite(LED_ALARME, ativo && fase ? HIGH : LOW);
  }
}

void escrever2(char *linha, uint8_t pos, uint8_t numero) {
  linha[pos] = '0' + numero / 10;
  linha[pos + 1] = '0' + numero % 10;
}

void formatarTela(const Tela &t, char *l0, char *l1, uint8_t &coluna) {
  memset(l0, ' ', 16); memset(l1, ' ', 16);
  l0[16] = l1[16] = '\0';
  coluna = 0;
  if (t.tocando) {
    memcpy(l0, "! ALARME TOCANDO", 16);
    memcpy(l1, "D5/SEL Zz 1s:FIM", 16);
  } else if (t.emSoneca) {
    memcpy(l0, "Zz SONECA ATIVA", 14);
    memcpy(l1, "RESTA ", 6);
    escrever2(l1, 6, t.sonecaSegundos / 60); l1[8] = ':';
    escrever2(l1, 9, t.sonecaSegundos % 60);
    memcpy(l1 + 12, "D5:X", 4);
  } else if (t.avisoEntrada) {
    memcpy(l0, "REPITA O COMANDO", 16);
    memcpy(l1, "Entrada ocupada", 15);
  } else if (t.editando) {
    static const char * const rotulos[] = {"HORA", "MIN", "SEG", "DIA", "MES", "ANO", "AL-H", "AL-M", "SONECA"};
    memcpy(l1, rotulos[t.campo], strlen(rotulos[t.campo]));
    memcpy(l1 + 7, "X<>Y+-OK", 8);
    if (t.campo <= 2) {
      memcpy(l0, "HORA ", 5);
      escrever2(l0, 5, t.cfg.hora); l0[7] = ':';
      escrever2(l0, 8, t.cfg.minuto); l0[10] = ':';
      escrever2(l0, 11, t.segundo);
      coluna = t.campo == 0 ? 5 : t.campo == 1 ? 8 : 11;
    } else if (t.campo <= 5) {
      memcpy(l0, "DATA ", 5);
      escrever2(l0, 5, t.cfg.dia); l0[7] = '/';
      escrever2(l0, 8, t.cfg.mes); l0[10] = '/';
      escrever2(l0, 11, t.cfg.ano / 100);
      escrever2(l0, 13, t.cfg.ano % 100);
      coluna = t.campo == 3 ? 5 : t.campo == 4 ? 8 : 11;
    } else if (t.campo <= 7) {
      memcpy(l0, "ALARME ", 7);
      escrever2(l0, 7, t.cfg.alarmeHora); l0[9] = ':';
      escrever2(l0, 10, t.cfg.alarmeMinuto);
      coluna = t.campo == 6 ? 7 : 10;
    } else {
      memcpy(l0, "SONECA ", 7);
      escrever2(l0, 7, t.cfg.sonecaMinutos);
      memcpy(l0 + 10, "min", 3);
      coluna = 7;
    }
  } else {
    escrever2(l0, 0, t.cfg.hora); l0[2] = ':';
    escrever2(l0, 3, t.cfg.minuto); l0[5] = ':';
    escrever2(l0, 6, t.segundo);
    memcpy(l0 + 9, t.alarmeLigado ? "AL" : "--", 2);
    escrever2(l0, 11, t.cfg.alarmeHora); l0[13] = ':';
    escrever2(l0, 14, t.cfg.alarmeMinuto);
    escrever2(l1, 0, t.cfg.dia); l1[2] = '/';
    escrever2(l1, 3, t.cfg.mes); l1[5] = '/';
    escrever2(l1, 6, t.cfg.ano / 100);
    escrever2(l1, 8, t.cfg.ano % 100);
    memcpy(l1 + 12, t.alarmeLigado ? "ON " : "OFF", 3);
  }
}

/* Diagnostico auxiliar: somente TaskDisplay escreve na Serial em execucao.
 * Nao participa da contagem e nao exige computador para operar o relogio.
 * T1 e millis usam o mesmo oscilador: uma referencia externa ainda e necessaria. */
void emitirDiagnostico(const Tela &t) {
  Serial.print(F("T1-LIVRE-2 t1=")); Serial.print(t.contagem);
  Serial.print(F(" ms=")); Serial.print(t.millisAmostra);
  Serial.print(F(" hora=")); Serial.print(t.horaReal);
  Serial.print(':'); Serial.print(t.minutoReal);
  Serial.print(':'); Serial.print(t.segundoReal);
  Serial.print(F(" ajustes=")); Serial.print(t.ajustesHora);
  Serial.print(F(" dtMax=")); Serial.print(t.maiorIntervalo);
  Serial.print(F(" timerOK=")); Serial.print(t.timerOk);
  Serial.print(F(" pilhas="));
  Serial.print(uxTaskGetStackHighWaterMark(tarefaRelogio)); Serial.print('/');
  Serial.print(uxTaskGetStackHighWaterMark(tarefaBotoes)); Serial.print('/');
  Serial.print(uxTaskGetStackHighWaterMark(tarefaAlarme)); Serial.print('/');
  Serial.println(uxTaskGetStackHighWaterMark(tarefaDisplay));
}

void TaskDisplay(void *parametros) {
  (void)parametros;
  Tela tela;
  char l0[17], l1[17], anterior0[17] = "", anterior1[17] = "";
  bool cursorVisivel = false;
  uint32_t ultimoDiagnostico = 0;
  bool primeiroDiagnostico = true;
  for (;;) {
    if (xQueueReceive(filaTela, &tela, portMAX_DELAY) != pdPASS) continue;
    uint8_t coluna;
    formatarTela(tela, l0, l1, coluna);
    /* So esta tarefa acessa o LCD apos setup. Nenhum mutex ou secao critica. */
    if (strcmp(l0, anterior0)) {
      lcd.setCursor(0, 0); lcd.print(l0); strcpy(anterior0, l0);
    }
    if (strcmp(l1, anterior1)) {
      lcd.setCursor(0, 1); lcd.print(l1); strcpy(anterior1, l1);
    }
    bool mostrarCursor = tela.editando && !tela.tocando && !tela.emSoneca && !tela.avisoEntrada;
    if (mostrarCursor != cursorVisivel) {
      if (mostrarCursor) lcd.cursor(); else lcd.noCursor();
      cursorVisivel = mostrarCursor;
    }
    if (mostrarCursor) lcd.setCursor(coluna, 0);
    if (primeiroDiagnostico ||
        (uint32_t)(tela.millisAmostra - ultimoDiagnostico) >= 10000UL) {
      emitirDiagnostico(tela);
      ultimoDiagnostico = tela.millisAmostra;
      primeiroDiagnostico = false;
    }
  }
}

void erroInicial(const char *recurso) {
  /* Apenas setup, antes do escalonador. Nao tenta iniciar sem recursos. */
  noTone(BUZZER);
  digitalWrite(LED_ALARME, LOW);
  lcd.clear(); lcd.print("ERRO INICIAL");
  lcd.setCursor(0, 1); lcd.print(recurso);
  for (;;) {}
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("T1-LIVRE-2 | Timer1 normal /1024 | contagens/s=15625"));
  Serial.print(F("F_CPU=")); Serial.println(F_CPU);
  Serial.println(F("pilhas=Relogio/Botoes/Alarme/Display; t1 e dtMax em contagens"));
  pinMode(JOY_SEL, INPUT_PULLUP);
  pinMode(BOTAO_SONECA, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_ALARME, OUTPUT);
  digitalWrite(BUZZER, LOW);
  digitalWrite(LED_ALARME, LOW);
  lcd.begin(16, 2);
  lcd.print("Alarme RTOS MEGA");

  /* Alocacao apenas na inicializacao. Filas e tarefas vivem ate o reset. */
  filaEventos = xQueueCreate(TAM_FILA_EVENTOS, sizeof(Evento));
  filaAlarme = xQueueCreate(1, sizeof(bool));
  filaTela = xQueueCreate(1, sizeof(Tela));
  if (!filaEventos || !filaAlarme || !filaTela) erroInicial("FILA");

  /* AVR: StackType_t vale 1 byte. Total solicitado: 1472 bytes + kernel/heap. */
  if (xTaskCreate(TaskRelogio, "Relogio", 448, NULL, 2, &tarefaRelogio) != pdPASS)
    erroInicial("TASK RELOGIO");
  if (xTaskCreate(TaskBotoes, "Botoes", 256, NULL, 2, &tarefaBotoes) != pdPASS)
    erroInicial("TASK BOTOES");
  if (xTaskCreate(TaskAlarme, "Alarme", 256, NULL, 3, &tarefaAlarme) != pdPASS)
    erroInicial("TASK ALARME");
  if (xTaskCreate(TaskDisplay, "Display", 512, NULL, 1, &tarefaDisplay) != pdPASS)
    erroInicial("TASK DISPLAY");
  /* A integracao Arduino_FreeRTOS inicia o escalonador depois de setup(). */
}

void loop() {}
