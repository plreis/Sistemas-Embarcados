// Ponto de entrada Arduino. A integracao Arduino_FreeRTOS inicia o kernel
// automaticamente depois de setup(). Todos os demais arquivos ficam nesta pasta.
void iniciarAplicacao();
void setup() {
  iniciarAplicacao();
}
void loop() {}
