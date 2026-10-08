#pragma once
#include <stdint.h>

namespace relogio {
constexpr uint32_t TICKS_SEGUNDO = 100;
constexpr uint8_t CAMPOS = 9;
enum class Evento : uint8_t {
  Selecionar,
  Encerrar,
  Soneca,
  Esquerda,
  Direita,
  Mais,
  Menos
};
enum class Aviso : uint8_t { Silencio, Tocando, Adiado };
struct DataHora {
  uint16_t ano;
  uint8_t mes, dia, hora, minuto, segundo;
};
struct Alarme {
  uint8_t hora, minuto, sonecaMinutos;
  bool habilitado;
};
struct Edicao {
  DataHora data;
  Alarme alarme;
  uint8_t campo;
  bool ativa, mudouHora, mudouData;
};
struct Estado {
  DataHora agora;
  Alarme alarme;
  Edicao edicao;
  Aviso aviso;
  uint32_t restante, ultimaOcorrencia;
  uint16_t fracao, ajustesHora;
};

Estado estadoInicial();
uint8_t diasNoMes(uint16_t ano, uint8_t mes);
void avancar(Estado &e, uint32_t ticks);
void comandar(Estado &e, Evento evento);
// Preenche exatamente 16 caracteres + '\0'. cursor=-1 desativa sublinhado.
void renderizar(const Estado &e, char (&linha0)[17], char (&linha1)[17], int8_t &cursor);
} // namespace relogio
