#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// =========================================================================
// PARTE 1: CONFIGURAÇÕES DE HARDWARE, PROTOCOLO E CAMADA FÍSICA (RX)
// =========================================================================
#define PIN_RX 5             // Pino DATA do módulo RF Receptor (escuta DATA/END)
#define PIN_TX 4             // Pino DATA do módulo RF Transmissor (envia ACK)

#define BIT_DURATION 1000    // 1000 us = 1 ms por bit (1000 bps)
#define HALF_BIT 500         // Metade do tempo de um bit (500 us)

#define FLAG_BYTE 0x7E
#define TYPE_DATA 0x01
#define TYPE_ACK  0x02
#define TYPE_END  0x03

enum ModoErro { ERRO_CHECKSUM = 1, ERRO_CRC8 = 2 };
ModoErro modoErroAtual = ERRO_CRC8; // Padrão: CRC-8 (idêntico ao TX)

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Estrutura para reordenar quadros (suporte completo ao Selective Repeat)
struct SlotRecepcao {
  bool recebido;
  uint8_t len;
  uint8_t data[16];
};

SlotRecepcao janelaRx[64];
int seqBase = -1; // Guarda o primeiro SEQ da mensagem atual

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

bool lerBitManchester(bool &bitLido, unsigned long timeoutMicros) {
  unsigned long inicio = micros();
  bool estadoInicial = digitalRead(PIN_RX);

  while (digitalRead(PIN_RX) == estadoInicial) {
    if (micros() - inicio > timeoutMicros) return false;
  }

  delayMicroseconds(HALF_BIT + (HALF_BIT / 2));
  bitLido = (digitalRead(PIN_RX) == LOW) ? 1 : 0;
  return true;
}

bool lerByte(uint8_t &byteLido) {
  byteLido = 0;
  for (int i = 7; i >= 0; i--) {
    bool bitAtual;
    if (!lerBitManchester(bitAtual, 5000)) return false;
    byteLido |= (bitAtual << i);
  }
  return true;
}

// =========================================================================
// PARTE 2: DETECÇÃO DE ERROS (CHECKSUM / CRC-8) E ENVIO DE ACK
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

void enviarQuadroACK(uint8_t seqConfirmado) {
  delay(15); // Tempo de guarda para evitar colisão no canal 433 MHz

  uint8_t fcs = calcularFCS(TYPE_ACK, seqConfirmado, 0, nullptr);

  enviarPreambulo();
  enviarByte(FLAG_BYTE);
  enviarByte(TYPE_ACK);
  enviarByte(seqConfirmado);
  enviarByte(0x00);
  enviarByte(fcs);

  digitalWrite(PIN_TX, LOW);
  Serial.printf("  [TX-ACK] Confirmacao ACK %d enviada!\n", seqConfirmado);
}

// =========================================================================
// PARTE 3: REORDENAÇÃO DE QUADROS, EXIBIÇÃO NO LCD E LOOP PRINCIPAL
// =========================================================================
void limparBufferRecepcao() {
  for (int i = 0; i < 64; i++) {
    janelaRx[i].recebido = false;
    janelaRx[i].len = 0;
  }
  seqBase = -1;
}

void montarEExibirEntrega(uint8_t seqEnd) {
  if (seqBase < 0) return;

  uint8_t bufferFinal[1024];
  size_t totalBytes = 0;

  // Calcula quantos quadros de dados existiram entre seqBase e seqEnd
  uint8_t qtdQuadros = (uint8_t)(seqEnd - (uint8_t)seqBase);

  for (uint8_t i = 0; i < qtdQuadros && i < 64; i++) {
    if (janelaRx[i].recebido) {
      memcpy(&bufferFinal[totalBytes], janelaRx[i].data, janelaRx[i].len);
      totalBytes += janelaRx[i].len;
    }
  }

  if (totalBytes == 0) {
    limparBufferRecepcao();
    return;
  }

  // Se começar com 'I' (0x49), é uma imagem para o script Python desenhar
  if (bufferFinal[0] == 0x49) {
    Serial.print("IMG_HEX:");
    for (size_t i = 1; i < totalBytes; i++) {
      Serial.printf("%02X", bufferFinal[i]);
    }
    Serial.println();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Emoji Recebido!");
    lcd.setCursor(0, 1);
    lcd.print("Veja no PC ->");
  } else {
    bufferFinal[totalBytes] = '\0';
    char* texto = (bufferFinal[0] == 0x54) ? (char*)&bufferFinal[1] : (char*)bufferFinal;

    Serial.printf("\n>>> MENSAGEM RECEBIDA: %s <<<\n\n", texto);

    lcd.clear();
    lcd.setCursor(0, 0);
    for (int i = 0; i < 16 && texto[i] != '\0'; i++) {
      lcd.print(texto[i]);
    }
    if (strlen(texto) > 16) {
      lcd.setCursor(0, 1);
      for (int i = 16; i < 32 && texto[i] != '\0'; i++) {
        lcd.print(texto[i]);
      }
    }
  }

  limparBufferRecepcao();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RX, INPUT);
  pinMode(PIN_TX, OUTPUT);
  digitalWrite(PIN_TX, LOW);

  limparBufferRecepcao();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Receptor RF 433");
  lcd.setCursor(0, 1);
  lcd.print("Modo: CRC-8");

  Serial.println("====================================================");
  Serial.println(" RECEPTOR RF 433 MHz - PRONTO (CRC-8 / JANELA RX)");
  Serial.println(" Digite '3' no Serial para alternar CRC-8/Checksum");
  Serial.println("====================================================\n");
}

void loop() {
  // Permite alternar entre CRC-8 e Checksum via Monitor Serial para combinar com o TX
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "3") {
      modoErroAtual = (modoErroAtual == ERRO_CRC8) ? ERRO_CHECKSUM : ERRO_CRC8;
      const char* nome = (modoErroAtual == ERRO_CRC8) ? "CRC-8" : "Checksum";
      Serial.printf(">>> Modo de Erro alterado para: %s <<<\n", nome);
      lcd.setCursor(0, 1);
      lcd.printf("Modo: %-10s", nome);
    }
  }

  static uint8_t shiftReg = 0;
  bool bitAtual;

  if (lerBitManchester(bitAtual, 10000)) {
    shiftReg = (shiftReg << 1) | bitAtual;

    if (shiftReg == FLAG_BYTE) {
      shiftReg = 0;

      uint8_t rxType, rxSeq, rxLen;
      if (!lerByte(rxType)) return;
      if (!lerByte(rxSeq))  return;
      if (!lerByte(rxLen))  return;

      if (rxLen > 16) {
        Serial.println("[ERRO] LEN invalido (>16). Quadro descartado.");
        return;
      }

      uint8_t rxData[16];
      for (uint8_t i = 0; i < rxLen; i++) {
        if (!lerByte(rxData[i])) return;
      }

      uint8_t rxFcs;
      if (!lerByte(rxFcs)) return;

      uint8_t fcsCalculado = calcularFCS(rxType, rxSeq, rxLen, rxData);

      if (rxFcs != fcsCalculado) {
        Serial.printf("[CORROMPIDO] SEQ %d falhou na verificacao! (Rec: 0x%02X | Calc: 0x%02X). Descartando...\n",
                      rxSeq, rxFcs, fcsCalculado);
        return;
      }

      Serial.printf("[RX-OK] Quadro valido | TYPE: 0x%02X | SEQ: %d | LEN: %d\n", rxType, rxSeq, rxLen);
      enviarQuadroACK(rxSeq);

      if (rxType == TYPE_DATA) {
        if (seqBase < 0) {
          seqBase = rxSeq; // Define o início da mensagem atual
        }
        uint8_t indice = (uint8_t)(rxSeq - (uint8_t)seqBase);
        if (indice < 64 && !janelaRx[indice].recebido) {
          janelaRx[indice].recebido = true;
          janelaRx[indice].len = rxLen;
          memcpy(janelaRx[indice].data, rxData, rxLen);
        }
      } 
      else if (rxType == TYPE_END) {
        Serial.println("[RX-END] Fim da transmissao recebido! Montando entrega...");
        montarEExibirEntrega(rxSeq);
      }
    }
  }
}