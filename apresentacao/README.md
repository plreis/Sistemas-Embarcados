# Apresentação à banca — Relógio e alarme com FreeRTOS

- [PowerPoint editável, com notas do apresentador](Apresentacao_Relogio_RTOS.pptx)
- [Slides em PDF](Apresentacao_Relogio_RTOS.pdf)
- [Roteiro de fala e preparação da demonstração](Roteiro_apresentacao.md)

A apresentação contém **13 slides principais**, para cerca de **12 minutos de exposição**, além da demonstração. Os **três slides finais** apoiam a arguição sobre registradores, concorrência, memória e referências.

| Slides | Assunto |
| --- | --- |
| 1–2 | Objetivo, requisitos e protótipo |
| 3–6 | RTOS, arquitetura, prioridades, filas e mutex |
| 7–8 | Temporizadores, Timer1 e atomicidade |
| 9–10 | Interface, alarme e soneca |
| 11–12 | Precisão e resultados |
| 13 | Conclusão e demonstração física |
| 14–16 | Apoio à arguição e fontes |

O PowerPoint inclui o roteiro nas notas de cada slide. O PDF mostra somente o conteúdo projetado. A apresentação utiliza as fotografias originais e o relatório atualizado, com operação local e computador somente como alimentação. O firmware é o mesmo da entrega.

## Reprodução dos arquivos

A fonte editável principal é o `.pptx`. O script também permite reconstruir o conjunto:

```bash
python3 -m pip install -r apresentacao/requirements.txt
python3 apresentacao/gerar_slides.py
libreoffice --headless --convert-to pdf --outdir apresentacao apresentacao/Apresentacao_Relogio_RTOS.pptx
```

O script utiliza a fonte DejaVu Sans e as imagens da pasta `fotos/`. A exportação foi realizada com LibreOffice 26.2.6.3.
