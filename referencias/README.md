# Trechos da biblioteca FreeRTOS

Extraídos da instalação FreeRTOS **11.1.0-3** utilizada na compilação. `origem.json` registra arquivo, intervalos de linhas e SHA-256 do arquivo-fonte completo. `// [...]` indica omissão; não faz parte da biblioteca. Os excertos preservam as instruções originais e retiram apenas recuo comum.

- [ticks.cpp](ticks.cpp): [FreeRTOSVariant.h](https://github.com/feilipu/Arduino_FreeRTOS_Library/blob/master/src/FreeRTOSVariant.h), linhas 46–48, 62–63.
- [milissegundos.cpp](milissegundos.cpp): [projdefs.h](https://github.com/feilipu/Arduino_FreeRTOS_Library/blob/master/src/projdefs.h), linhas 42–42.
- [periodicidade.cpp](periodicidade.cpp): [tasks.c](https://github.com/feilipu/Arduino_FreeRTOS_Library/blob/master/src/tasks.c), linhas 2359–2359, 2393–2393, 2395–2402.
- [sobrescrita.cpp](sobrescrita.cpp): [queue.h](https://github.com/feilipu/Arduino_FreeRTOS_Library/blob/master/src/queue.h), linhas 597–598.
- [copia.cpp](copia.cpp): [queue.c](https://github.com/feilipu/Arduino_FreeRTOS_Library/blob/master/src/queue.c), linhas 2427–2427.
- [inicializacao.cpp](inicializacao.cpp): [variantHooks.cpp](https://github.com/feilipu/Arduino_FreeRTOS_Library/blob/master/src/variantHooks.cpp), linhas 57–58.

O arquivo `LICENCAS.txt` conserva os avisos de copyright e licença MIT do kernel e da integração AVR. Os links apontam ao projeto de origem; o hash identifica a cópia local examinada.
