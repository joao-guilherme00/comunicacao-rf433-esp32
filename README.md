# 📡 Sistema de Comunicação Confiável via Rádio (RF 433 MHz + ESP32)

Projeto desenvolvido para a disciplina de **Transmissão e Comunicação de Dados**, implementando do zero um sistema de comunicação sem fio bidirecional (*half-duplex*) entre dois microcontroladores **ESP32** utilizando módulos **RF 433 MHz** (`FS1000A` e `MX-RM-5V`).

Toda a **Camada Física** (Codificação Manchester via *bit-banging*) e a **Camada de Enlace** (Enquadramento, CRC-8/Checksum e controle de fluxo ARQ com janela deslizante) foram implementadas manualmente no firmware, sem bibliotecas prontas de rádio.

## 👥 Integrantes do Grupo
* **[Seu Nome Completo]**
* **[Nome Completo do Colega 1]**
* **[Nome Completo do Colega 2]**

---

## 📂 Estrutura do Repositório

```text
├── Projeto_TX/
│   ├── transmissor_esp32/
│   │   └── transmissor_esp32.ino  # Firmware do Nó A (Transmissor + Menu Interativo)
│   ├── python_sprits/
│   │   └── transmissor.py         # Script Python PC -> ESP32 TX
│   └── emojis/                    # Banco de imagens/emojis (.png) prontos para envio
│
└── Projeto_RX/
    ├── Receptor_esp32/
    │   └── receptor_esp32.ino     # Firmware do Nó B (Receptor + LCD I2C + Reordenação)
    ├── python_sprits/
    │   └── receptor.py            # Script Python ESP32 RX -> PC (ASCII + PNG)
    └── emojis/                    # Pasta de referência/saída de imagens
```

---

## 🛠️ Lista de Materiais (Hardware)
* **2x Computadores** com cabos USB de dados
* **2x Microcontroladores ESP32** (DevKit V1 - WROOM-32)
* **4x Módulos RF 433 MHz** (**2x** Transmissores `FS1000A` e **2x** Receptores `MX-RM-5V`)
* **4x Fios de cobre rígido de 17,3 cm** soldados no pino `ANT` de cada módulo (antena de 1/4 de onda)
* **1x Display LCD 16x2 com Módulo I2C** (endereço padrão `0x27`)
* **2x Conversores de Nível Lógico Bidirecionais** (4 canais, `3.3V ↔ 5V`)
* **2x Protoboards** (830 pontos) e **Cabos Jumpers**

---

## 🔌 Esquemático de Ligação (Pinagem)

> **Atenção:** Todos os pinos `GND` são interligados em comum. Nos conversores de nível lógico, o pino `LV` recebe `3.3V` do ESP32 e o pino `HV` recebe `5V (VIN)`.

### Lado Transmissor (Nó A)

| Componente | Pino | Conversor Lógico | Pino ESP32 | Função |
| :--- | :--- | :--- | :--- | :--- |
| **TX FS1000A** | `VCC` / `GND` | — | `VIN (5V)` / `GND` | Alimentação (maior alcance) |
| **TX FS1000A** | `DATA` | `HV1` → `LV1` | **GPIO 4** (`PIN_TX`) | Envia quadros `DATA` e `END` |
| **RX MX-RM-5V** | `VCC` / `GND` | — | `VIN (5V)` / `GND` | Alimentação do receptor de retorno |
| **RX MX-RM-5V** | `DATA` | `HV2` → `LV2` | **GPIO 5** (`PIN_RX`) | Escuta os quadros de confirmação (`ACK`) |

### Lado Receptor (Nó B)

| Componente | Pino | Conversor Lógico | Pino ESP32 | Função |
| :--- | :--- | :--- | :--- | :--- |
| **RX MX-RM-5V** | `VCC` / `GND` | — | `VIN (5V)` / `GND` | Alimentação do receptor principal |
| **RX MX-RM-5V** | `DATA` | `HV1` → `LV1` | **GPIO 5** (`PIN_RX`) | Recebe os quadros `DATA` e `END` |
| **TX FS1000A** | `VCC` / `GND` | — | `VIN (5V)` / `GND` | Alimentação do transmissor de retorno |
| **TX FS1000A** | `DATA` | `HV2` → `LV2` | **GPIO 4** (`PIN_TX`) | Envia quadros de confirmação (`ACK`) |
| **LCD 16x2 I2C** | `VCC` / `GND` | — | `VIN (5V)` / `GND` | Alimentação do display |
| **LCD 16x2 I2C** | `SDA` / `SCL` | `HV3`/`HV4` → `LV3`/`LV4` | **GPIO 21** / **GPIO 22** | Comunicação I2C (`0x27`) |

---

## ⚙️ Especificações Técnicas do Protocolo
* **Camada Física:** Modulação OOK a **1000 bps** (`BIT_DURATION = 1000 µs`, `HALF_BIT = 500 µs`) com **Codificação Manchester** e preâmbulo de sincronismo de 3 bytes (`0xAA, 0xAA, 0xAA`).
* **Estrutura do Quadro (Máx. 21 bytes):**
  `[FLAG (0x7E)] | [TYPE] | [SEQ] | [LEN] | [DATA (0..16 bytes)] | [FCS]`
  * `TYPE`: `0x01` (`DATA`), `0x02` (`ACK`), `0x03` (`END`).
* **Detecção de Erros (FCS):** **CRC-8** (polinômio `0x07`, padrão) ou **Checksum de 8 bits**.
* **Controle de Fluxo (ARQ):** **Selective Repeat** (`WINDOW_SIZE = 3`, padrão), **Go-Back-N** (`WINDOW_SIZE = 3`) ou **Stop-and-Wait**, com `TIMEOUT_ACK = 550 ms` e limite de `MAX_RETRIES = 10`.

---

## 🚀 Como Reproduzir e Executar

### Passo 1: Gravação dos Firmwares (Arduino IDE)
1. Instale o pacote de placas **ESP32** na Arduino IDE e a biblioteca **`LiquidCrystal_I2C`**.
2. Carregue `Projeto_TX/transmissor_esp32/transmissor_esp32.ino` no **ESP32 Transmissor**.
3. Carregue `Projeto_RX/Receptor_esp32/receptor_esp32.ino` no **ESP32 Receptor**.
4. Ao ligar, o display LCD exibirá `"Receptor RF 433"` e `"Modo: CRC-8"`.

### Passo 2: Instalação das Dependências Python
Nos dois computadores, abra o terminal e instale as bibliotecas necessárias:

```bash
pip install pyserial pillow
```

### Passo 3: Configuração das Portas COM e Execução
1. **Feche o Monitor Serial** da Arduino IDE nos dois computadores (para liberar as portas COM).
2. Abra `Projeto_TX/python_sprits/transmissor.py` e `Projeto_RX/python_sprits/receptor.py` e ajuste a variável `PORTA_USB` (atualmente `"COM4"`) para a porta serial de cada placa.
3. Abra o terminal na pasta raiz de cada lado e execute:
   * **No PC Receptor (dentro de `Projeto_RX/`):**
     ```bash
     python python_sprits/receptor.py
     ```
   * **No PC Transmissor (dentro de `Projeto_TX/`):**
     ```bash
     python python_sprits/transmissor.py
     ```

### Passo 4: Comandos de Teste
* **Texto no LCD:** Digite qualquer frase (ex.: `Ola Engenharia!`) no terminal do `transmissor.py` e pressione `Enter`. O texto será exibido no LCD do receptor.
* **Injeção de Erro (ARQ):** Digite `erro` e envie uma mensagem em seguida. O firmware inverterá o byte `FCS` (`fcs ^= 0xFF`) do próximo quadro; o receptor descartará o pacote corrompido e o transmissor fará a retransmissão automática após o *timeout*.
* **Envio de Imagem / Emoji:** Digite o nome de qualquer imagem disponível dentro da pasta `Projeto_TX/emojis/` (ex.: `terra.png`). A imagem será convertida para bitmap `24x24` (72 bytes), enviada em 5 quadros `DATA` + 1 `END` via rádio, desenhada em ASCII no terminal receptor e salva/aberta como `emoji_recebido.png` (`360x360`).
* **Menu Direto via Monitor Serial (Opcional):** Caso abra o Serial do TX diretamente (`115200 bps`), utilize as opções `` Texto, `` Emoji interno 16x16, `` Alternar CRC-8/Checksum (digite `3` no RX também para sincronizar), `` Alternar Stop-and-Wait/Go-Back-N/Selective Repeat e `` Simular erro.
