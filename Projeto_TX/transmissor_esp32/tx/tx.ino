#include <Arduino.h>

// =========================================================================
// PARTE 1: CONFIGURAÇÕES DE HARDWARE, PROTOCOLO E CAMADA FÍSICA
// =========================================================================
#define PIN_TX 4             // Pino DATA do módulo RF Transmissor (433 MHz)
#define PIN_RX 5             // Pino DATA do módulo RF Receptor (433 MHz - escuta ACK)

#define BIT_DURATION 1000    // 1000 us = 1 ms por bit (1000 bps)
#define HALF_BIT 500         // Metade do tempo de um bit (500 us)
#define TIMEOUT_ACK 550      // Tempo de espera por ACK (ms)
#define MAX_RETRIES 10       // Limite de tentativas por janela/quadro
#define WINDOW_SIZE 3        // Tamanho da janela para Go-Back-N e Selective Repeat

#define FLAG_BYTE 0x7E
#define TYPE_DATA 0x01
#define TYPE_ACK  0x02
#define TYPE_END  0x03

enum ModoErro { ERRO_CHECKSUM = 1, ERRO_CRC8 = 2 };
enum ModoFluxo { FLUXO_STOP_WAIT = 1, FLUXO_GO_BACK_N = 2, FLUXO_SELECTIVE_REPEAT = 3 };

ModoErro modoErroAtual = ERRO_CRC8;                  // Padrão: CRC-8 (3 pts)
ModoFluxo modoFluxoAtual = FLUXO_SELECTIVE_REPEAT;   // Padrão: Selective Repeat (3 pts)

bool simularErroProposital = false;
bool aguardandoTextoMenu = false;
uint8_t seqGlobal = 0;

struct QuadroBuffer {
  uint8_t type;
  uint8_t seq;
  uint8_t len;
  uint8_t data[16];
  bool confirmado;
};

// Emoji interno de teste: Carinha Feliz 16x16 pixels (32 bytes)
const uint8_t EMOJI_SORRISO[32] = {
  0x07, 0xE0, 0x18, 0x18, 0x20, 0x04, 0x40, 0x02,
  0x4C, 0x32, 0x8C, 0x31, 0x80, 0x01, 0x80, 0x01,
  0x80, 0x01, 0x90, 0x09, 0x88, 0x11, 0x47, 0xE2,
  0x40, 0x02, 0x20, 0x04, 0x18, 0x18, 0x07, 0xE0
};

void enviarBitManchester(bool bitVal) {
  if (bitVal) {
    digitalWrite(PIN_TX, HIGH);
    delayMicroseconds(HALF_BIT);
    digitalWrite(PIN_TX, LOW);
    delayMicroseconds(HALF_BIT);
  } else {
    digitalWrite(PIN_TX, LOW);
    delayMicroseconds(HALF_BIT);
    digitalWrite(PIN_TX, HIGH);
    delayMicroseconds(HALF_BIT);
  }
}

void enviarByte(uint8_t dado) {
  for (int i = 7; i >= 0; i--) {
    enviarBitManchester((dado >> i) & 0x01);
  }
}

void enviarPreambulo() {
  for (int i = 0; i < 3; i++) {
    enviarByte(0xAA);
  }
}

bool lerBitManchester(bool &bitLido, unsigned long deadlineMillis) {
  bool estadoInicial = digitalRead(PIN_RX);
  while (digitalRead(PIN_RX) == estadoInicial) {
    if (millis() > deadlineMillis) return false;
  }
  delayMicroseconds(HALF_BIT + (HALF_BIT / 2));
  bitLido = (digitalRead(PIN_RX) == LOW) ? 1 : 0;
  return true;
}

// =========================================================================
// PARTE 2: DETECÇÃO DE ERROS (CHECKSUM / CRC-8) E QUADROS
// =========================================================================
uint8_t calcularChecksum(uint8_t type, uint8_t seq, uint8_t len, const uint8_t* data) {
  uint16_t soma = type + seq + len;
  for (uint8_t i = 0; i < len; i++) {
    soma += data[i];
  }
  return (uint8_t)(soma & 0xFF);
}

uint8_t atualizarCRC8(uint8_t crc, uint8_t dado) {
  crc ^= dado;
  for (uint8_t i = 0; i < 8; i++) {
    if (crc & 0x80) {
      crc = (crc << 1) ^ 0x07;
    } else {
      crc <<= 1;
    }
  }
  return crc;
}

uint8_t calcularCRC8(uint8_t type, uint8_t seq, uint8_t len, const uint8_t* data) {
  uint8_t crc = 0x00;
  crc = atualizarCRC8(crc, type);
  crc = atualizarCRC8(crc, seq);
  crc = atualizarCRC8(crc, len);
  for (uint8_t i = 0; i < len; i++) {
    crc = atualizarCRC8(crc, data[i]);
  }
  return crc;
}

uint8_t calcularFCS(uint8_t type, uint8_t seq, uint8_t len, const uint8_t* data) {
  if (modoErroAtual == ERRO_CRC8) {
    return calcularCRC8(type, seq, len, data);
  }
  return calcularChecksum(type, seq, len, data);
}

void transmitirQuadro(uint8_t type, uint8_t seq, uint8_t len, const uint8_t* data) {
  if (len > 16) len = 16;
  uint8_t fcs = calcularFCS(type, seq, len, data);

  if (simularErroProposital) {
    Serial.printf("  [!] INJETANDO ERRO PROPOSITAL NO QUADRO SEQ %d!\n", seq);
    fcs ^= 0xFF;
    simularErroProposital = false;
  }

  enviarPreambulo();
  enviarByte(FLAG_BYTE);
  enviarByte(type);
  enviarByte(seq);
  enviarByte(len);
  for (uint8_t i = 0; i < len; i++) {
    enviarByte(data[i]);
  }
  enviarByte(fcs);

  digitalWrite(PIN_TX, LOW);
}

int escutarQualquerACK(unsigned long deadlineMillis) {
  uint8_t shiftReg = 0;

  while (millis() < deadlineMillis) {
    bool bitAtual;
    if (!lerBitManchester(bitAtual, deadlineMillis)) return -1;

    shiftReg = (shiftReg << 1) | bitAtual;
    if (shiftReg == FLAG_BYTE) {
      shiftReg = 0;
      uint8_t bufferACK[4];
      bool leituraOk = true;

      for (int b = 0; b < 4; b++) {
        uint8_t byteMontado = 0;
        for (int bit = 7; bit >= 0; bit--) {
          bool bVal;
          if (!lerBitManchester(bVal, deadlineMillis)) {
            leituraOk = false;
            break;
          }
          byteMontado |= (bVal << bit);
        }
        if (!leituraOk) break;
        bufferACK[b] = byteMontado;
      }

      if (!leituraOk) return -1;

      uint8_t rxType = bufferACK[0];
      uint8_t rxSeq  = bufferACK[1];
      uint8_t rxLen  = bufferACK[2];
      uint8_t rxFcs  = bufferACK[3];

      uint8_t fcsCalc = calcularFCS(rxType, rxSeq, rxLen, nullptr);
      if (rxFcs == fcsCalc && rxType == TYPE_ACK) {
        return (int)rxSeq;
      }
    }
  }
  return -1;
}
// =========================================================================
// PARTE 3: CONTROLE DE FLUXO (SW, GBN, SR) E MENU INTERATIVO
// =========================================================================
void enviarComStopAndWait(QuadroBuffer* fila, int totalQuadros) {
  for (int i = 0; i < totalQuadros; i++) {
    int tentativas = 0;
    while (tentativas < MAX_RETRIES) {
      tentativas++;
      Serial.printf("[Stop-and-Wait] Enviando SEQ %d (Tentativa %d)...\n", fila[i].seq, tentativas);
      transmitirQuadro(fila[i].type, fila[i].seq, fila[i].len, fila[i].data);

      int ackSeq = escutarQualquerACK(millis() + TIMEOUT_ACK);
      if (ackSeq == fila[i].seq) {
        Serial.printf("  [OK] ACK %d confirmado!\n", ackSeq);
        fila[i].confirmado = true;
        break;
      }
      Serial.printf("  [TIMEOUT] Sem ACK para SEQ %d. Retransmitindo...\n", fila[i].seq);
    }
    if (!fila[i].confirmado) {
      Serial.println("[FALHA] Abortando transmissao por excesso de erros.");
      return;
    }
  }
}

void enviarComGoBackN(QuadroBuffer* fila, int totalQuadros) {
  int base = 0;
  int tentativas = 0;

  while (base < totalQuadros && tentativas < MAX_RETRIES) {
    int fimJanela = min(base + WINDOW_SIZE, totalQuadros);
    Serial.printf("\n[Go-Back-N] Transmitindo Janela [SEQ %d ate %d]...\n", fila[base].seq, fila[fimJanela - 1].seq);

    for (int i = base; i < fimJanela; i++) {
      transmitirQuadro(fila[i].type, fila[i].seq, fila[i].len, fila[i].data);
      delay(35);
    }

    unsigned long deadline = millis() + (TIMEOUT_ACK * (fimJanela - base));
    while (base < fimJanela && millis() < deadline) {
      int ackSeq = escutarQualquerACK(deadline);
      if (ackSeq >= 0) {
        for (int i = base; i < fimJanela; i++) {
          if (fila[i].seq == (uint8_t)ackSeq) {
            Serial.printf("  [OK] ACK %d recebido! Deslizando base da janela.\n", ackSeq);
            for (int k = base; k <= i; k++) fila[k].confirmado = true;
            base = i + 1;
            tentativas = 0;
            break;
          }
        }
      }
    }

    if (base < fimJanela) {
      tentativas++;
      Serial.printf("  [TIMEOUT GBN] Perda detectada! Voltando e retransmitindo a partir do SEQ %d...\n", fila[base].seq);
    }
  }
}

void enviarComSelectiveRepeat(QuadroBuffer* fila, int totalQuadros) {
  int base = 0;
  int tentativas = 0;

  while (base < totalQuadros && tentativas < MAX_RETRIES) {
    int fimJanela = min(base + WINDOW_SIZE, totalQuadros);
    bool enviouAlgum = false;

    for (int i = base; i < fimJanela; i++) {
      if (!fila[i].confirmado) {
        if (!enviouAlgum) {
          Serial.printf("\n[Selective Repeat] Enviando quadros pendentes na janela [%d..%d]:\n", 
                        fila[base].seq, fila[fimJanela - 1].seq);
          enviouAlgum = true;
        }
        Serial.printf("  -> Enviando SEQ %d\n", fila[i].seq);
        transmitirQuadro(fila[i].type, fila[i].seq, fila[i].len, fila[i].data);
        delay(35);
      }
    }

    unsigned long deadline = millis() + (TIMEOUT_ACK * 2);
    while (millis() < deadline) {
      bool todosConfirmados = true;
      for (int i = base; i < fimJanela; i++) {
        if (!fila[i].confirmado) todosConfirmados = false;
      }
      if (todosConfirmados) break;

      int ackSeq = escutarQualquerACK(deadline);
      if (ackSeq >= 0) {
        for (int i = base; i < fimJanela; i++) {
          if (fila[i].seq == (uint8_t)ackSeq && !fila[i].confirmado) {
            fila[i].confirmado = true;
            Serial.printf("  [OK] ACK individual %d confirmado!\n", ackSeq);
          }
        }
      }
    }

    int baseAnterior = base;
    while (base < totalQuadros && fila[base].confirmado) {
      base++;
    }

    if (base == baseAnterior) {
      tentativas++;
      Serial.println("  [TIMEOUT SR] Retransmitindo APENAS os quadros nao confirmados da janela...");
    } else {
      tentativas = 0;
    }
  }
}

void enviarPacoteCompleto(const uint8_t* bufferTotal, size_t tamanhoTotal) {
  int numQuadrosDados = (tamanhoTotal + 15) / 16;
  int totalQuadros = numQuadrosDados + 1;

  if (totalQuadros > 64) {
    Serial.println("[ERRO] Mensagem muito grande para o buffer!");
    return;
  }

  QuadroBuffer fila[64];
  size_t offset = 0;

  for (int i = 0; i < numQuadrosDados; i++) {
    uint8_t bloco = (tamanhoTotal - offset > 16) ? 16 : (tamanhoTotal - offset);
    fila[i].type = TYPE_DATA;
    fila[i].seq = seqGlobal++;
    fila[i].len = bloco;
    memcpy(fila[i].data, &bufferTotal[offset], bloco);
    fila[i].confirmado = false;
    offset += bloco;
  }

  fila[numQuadrosDados].type = TYPE_END;
  fila[numQuadrosDados].seq = seqGlobal++;
  fila[numQuadrosDados].len = 0;
  fila[numQuadrosDados].confirmado = false;

  unsigned long tempoInicio = millis();

  if (modoFluxoAtual == FLUXO_STOP_WAIT) {
    enviarComStopAndWait(fila, totalQuadros);
  } else if (modoFluxoAtual == FLUXO_GO_BACK_N) {
    enviarComGoBackN(fila, totalQuadros);
  } else {
    enviarComSelectiveRepeat(fila, totalQuadros);
  }

  unsigned long tempoTotal = millis() - tempoInicio;
  Serial.printf("\n=== TRANSMISSAO FINALIZADA EM %lu ms ===\n", tempoTotal);
}

void exibirMenu() {
  const char* nomeErro = (modoErroAtual == ERRO_CRC8) ? "CRC-8" : "Checksum";
  const char* nomeFluxo = (modoFluxoAtual == FLUXO_STOP_WAIT) ? "Stop-and-Wait" :
                          (modoFluxoAtual == FLUXO_GO_BACK_N) ? "Go-Back-N" : "Selective Repeat";

  Serial.println("\n+-------------------------------------------------------+");
  Serial.println("|        TRANSMISSOR RF 433 MHz - PAINEL DE CONTROLE    |");
  Serial.println("+-------------------------------------------------------+");
  Serial.printf ("| Algoritmo de Erro : %-33s |\n", nomeErro);
  Serial.printf ("| Controle de Fluxo : %-33s |\n", nomeFluxo);
  Serial.printf ("| Simulacao de Erro : %-33s |\n", simularErroProposital ? "ATIVADA (Proximo quadro)" : "Desativada");
  Serial.println("+-------------------------------------------------------+");
  Serial.println("| [1] Enviar Mensagem de Texto (LCD)                    |");
  Serial.println("| [2] Enviar Emoji de Teste Interno (16x16)             |");
  Serial.println("| [3] Alternar Deteccao de Erro (Checksum <-> CRC-8)    |");
  Serial.println("| [4] Alternar Controle de Fluxo (SW -> GBN -> SR)      |");
  Serial.println("| [5] Ativar/Desativar Simulacao de Erro (/erro)        |");
  Serial.println("+-------------------------------------------------------+");
  Serial.print("> Escolha uma opcao (1-5) ou envie via Python: ");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TX, OUTPUT);
  pinMode(PIN_RX, INPUT);
  digitalWrite(PIN_TX, LOW);

  delay(1000);
  exibirMenu();
}

void loop() {
  if (Serial.available() > 0) {
    String entrada = Serial.readStringUntil('\n');
    entrada.trim();
    if (entrada.length() == 0) return;

    if (aguardandoTextoMenu) {
      aguardandoTextoMenu = false;
      Serial.printf("\n>>> Enviando texto: \"%s\" <<<\n", entrada.c_str());
      enviarPacoteCompleto((const uint8_t*)entrada.c_str(), entrada.length());
      exibirMenu();
      return;
    }

    if (entrada.startsWith("IMG_HEX:")) {
      String hexStr = entrada.substring(8);
      size_t numBytes = hexStr.length() / 2;
      uint8_t bufferImagem[256];
      bufferImagem[0] = 0x49; // Prefixo 'I' de Imagem

      for (size_t i = 0; i < numBytes && (i + 1) < sizeof(bufferImagem); i++) {
        String byteHex = hexStr.substring(i * 2, i * 2 + 2);
        bufferImagem[i + 1] = (uint8_t)strtol(byteHex.c_str(), NULL, 16);
      }

      Serial.printf("\n>>> [PYTHON] Enviando Emoji (%d bytes) <<<\n", numBytes + 1);
      enviarPacoteCompleto(bufferImagem, numBytes + 1);
      exibirMenu();
    }
    else if (entrada == "1") {
      aguardandoTextoMenu = true;
      Serial.print("\nDigite a mensagem para o LCD e pressione ENTER: ");
    }
    else if (entrada == "2") {
      Serial.println("\n>>> Enviando Emoji Interno 16x16 <<<");
      uint8_t pacoteSorriso[33];
      pacoteSorriso[0] = 0x49;
      memcpy(&pacoteSorriso[1], EMOJI_SORRISO, 32);
      enviarPacoteCompleto(pacoteSorriso, 33);
      exibirMenu();
    }
    else if (entrada == "3") {
      modoErroAtual = (modoErroAtual == ERRO_CRC8) ? ERRO_CHECKSUM : ERRO_CRC8;
      Serial.println("\n>>> Algoritmo de Erro alterado! (Lembre-se de alinhar no RX) <<<");
      exibirMenu();
    }
    else if (entrada == "4") {
      if (modoFluxoAtual == FLUXO_STOP_WAIT) modoFluxoAtual = FLUXO_GO_BACK_N;
      else if (modoFluxoAtual == FLUXO_GO_BACK_N) modoFluxoAtual = FLUXO_SELECTIVE_REPEAT;
      else modoFluxoAtual = FLUXO_STOP_WAIT;
      Serial.println("\n>>> Algoritmo de Controle de Fluxo alterado! <<<");
      exibirMenu();
    }
    else if (entrada == "5" || entrada == "/erro") {
      simularErroProposital = !simularErroProposital;
      Serial.printf("\n>>> Simulacao de Erro: %s <<<\n", simularErroProposital ? "ATIVADA" : "DESATIVADA");
      exibirMenu();
    }
    else {
      Serial.printf("\n>>> Enviando texto direto: \"%s\" <<<\n", entrada.c_str());
      enviarPacoteCompleto((const uint8_t*)entrada.c_str(), entrada.length());
      exibirMenu();
    }
  }
}