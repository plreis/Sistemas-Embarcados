#include "RelogioTipos.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace relogio;
static void passou(const char *nome) {
  std::cout << "PASS " << nome << '\n';
}
static Estado preparo(uint8_t h, uint8_t m, uint8_t s) {
  Estado e = estadoInicial();
  e.agora = {2026, 10, 8, h, m, s};
  e.alarme = {7, 0, 1, true};
  return e;
}
int main() {
  Estado e = preparo(12, 0, 0);
  avancar(e, 2160000);
  assert(e.agora.hora == 18 && e.agora.minuto == 0 && e.agora.segundo == 0 &&
         e.fracao == 0);
  passou("seis horas nominais sem truncamento");
  e = preparo(23, 59, 59);
  e.agora.dia = 31;
  e.agora.mes = 12;
  avancar(e, 100);
  assert(e.agora.ano == 2027 && e.agora.mes == 1 && e.agora.dia == 1 && e.agora.hora == 0);
  for (uint16_t ano : {2000, 2026, 2028, 2099}) {
    e = preparo(23, 59, 59);
    e.agora.ano = ano;
    e.agora.mes = 2;
    e.agora.dia = 28;
    avancar(e, 100);
    bool bis = ano == 2000 || ano == 2028;
    assert(e.agora.mes == (bis ? 2 : 3) && e.agora.dia == (bis ? 29 : 1));
  }
  passou("calendario, bissextos e virada de ano");
  e = preparo(6, 59, 59);
  avancar(e, 100);
  assert(e.aviso == Aviso::Tocando && e.restante == 6000);
  comandar(e, Evento::Encerrar);
  avancar(e, 5900);
  assert(e.aviso == Aviso::Silencio);
  passou("disparo em segundo zero e encerramento sem redisparo");
  e = preparo(6, 59, 59);
  avancar(e, 7100);
  assert(e.agora.hora == 7 && e.agora.minuto == 1 && e.agora.segundo == 10 &&
         e.aviso == Aviso::Silencio);
  passou("recuperacao de atraso respeita duracao do alarme");
  e = preparo(23, 59, 50);
  e.aviso = Aviso::Tocando;
  e.restante = 6000;
  Estado b = e;
  comandar(e, Evento::Soneca);
  comandar(b, Evento::Selecionar);
  assert(e.restante == b.restante && e.aviso == b.aviso);
  avancar(e, 6000);
  assert(e.agora.hora == 0 && e.agora.segundo == 50 && e.aviso == Aviso::Tocando);
  avancar(e, 6000);
  assert(e.aviso == Aviso::Silencio);
  passou("soneca unificada atraves da meia-noite");
  e = preparo(12, 0, 0);
  comandar(e, Evento::Selecionar);
  for (int i = 0; i < 6; ++i)
    comandar(e, Evento::Direita);
  comandar(e, Evento::Mais);
  avancar(e, 1250);
  comandar(e, Evento::Selecionar);
  assert(e.agora.segundo == 12 && e.fracao == 50 && e.ajustesHora == 0 &&
         e.alarme.hora == 8);
  passou("edicao de alarme preserva calendario e fracao corrente");
  comandar(e, Evento::Selecionar);
  comandar(e, Evento::Direita);
  comandar(e, Evento::Direita);
  comandar(e, Evento::Mais);
  comandar(e, Evento::Selecionar);
  assert(e.agora.segundo == 13 && e.fracao == 0 && e.ajustesHora == 1);
  passou("SEG aplicado com nova fase civil e contador de ajustes");
  e = preparo(12, 0, 0);
  comandar(e, Evento::Selecionar);
  comandar(e, Evento::Mais);
  comandar(e, Evento::Soneca);
  assert(!e.edicao.ativa && e.agora.hora == 12);
  e.aviso = Aviso::Tocando;
  e.restante = 6000;
  comandar(e, Evento::Soneca);
  comandar(e, Evento::Soneca);
  assert(!e.alarme.habilitado && e.aviso == Aviso::Silencio);
  passou("D5 cancela rascunho ou soneca conforme estado");
  Botao sel;
  assert(sel.ler(true, 0xfffffff0, true) == 0);
  assert(sel.ler(true, 0xfffffff5, true) == 0);
  assert(sel.ler(true, 90, true) == 2);
  assert(sel.ler(true, 150, true) == 0);
  assert(sel.ler(false, 151, true) == 0);
  assert(sel.ler(false, 157, true) == 0);
  Botao d5;
  assert(d5.ler(true, 0, false) == 0);
  assert(d5.ler(false, 2, false) == 0);
  assert(d5.ler(true, 3, false) == 0);
  assert(d5.ler(true, 7, false) == 0);
  assert(d5.ler(true, 8, false) == 1);
  assert(d5.ler(true, 200, false) == 0);
  passou("debounce, clique longo unico, botao segurado e rollover");
  Eixo eixo;
  assert(eixo.ler(1, 0) == 1);
  assert(eixo.ler(1, 34) == 0);
  assert(eixo.ler(1, 35) == 1);
  assert(eixo.ler(0, 36) == 0);
  assert(eixo.ler(-1, 37) == -1);
  passou("navegacao e repeticao controlada");
  // Equivalencia entre intervalo grande e fragmentado para calendario e prazos.
  uint32_t rng = 17;
  for (int i = 0; i < 1000; ++i) {
    rng = rng * 1664525U + 1013904223U;
    Estado a = preparo(6, 59, 30), b = a;
    uint32_t dt = rng % 20000;
    avancar(a, dt);
    for (uint32_t sobra = dt; sobra;) {
      uint32_t n = sobra > 37 ? 37 : sobra;
      avancar(b, n);
      sobra -= n;
    }
    assert(a.agora.hora == b.agora.hora && a.agora.minuto == b.agora.minuto &&
           a.agora.segundo == b.agora.segundo);
    assert(a.fracao == b.fracao && a.aviso == b.aviso && a.restante == b.restante &&
           a.ultimaOcorrencia == b.ultimaOcorrencia);
  }
  passou("1000 intervalos inteiros versus fragmentados");
  for (int modo = 0; modo < 3; ++modo)
    for (int campo = 0; campo < 9; ++campo) {
      e = estadoInicial();
      e.edicao = {e.agora, e.alarme, (uint8_t)campo, true, false, false};
      e.aviso = (Aviso)modo;
      e.restante = 60000;
      char a[17], b[17];
      int8_t cur;
      renderizar(e, a, b, cur);
      assert(a[16] == 0 && b[16] == 0 && std::strlen(a) == 16 && std::strlen(b) == 16 &&
             cur < 16);
    }
  passou("LCD com 16 caracteres, terminador e cursor valido");
  uint32_t inicio = 0xfffffff0U, final = 0x00000020U;
  assert((uint32_t)(final - inicio) == 48);
  e = estadoInicial();
  avancar(e, (uint32_t)(final - inicio));
  assert(e.fracao == 48);
  passou("diferenca de amostras atravessa rollover uint32_t");
}
