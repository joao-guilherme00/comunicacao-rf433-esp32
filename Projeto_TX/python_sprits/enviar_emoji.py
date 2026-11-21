import serial
import time
import os
from PIL import Image

# === CONFIGURAÇÃO ===
PORTA_USB = "COM4"  # Troque pela porta COM do seu ESP32 (veja no Arduino IDE)
VELOCIDADE = 115200
TAMANHO = 24        # 24x24 pixels = 576 bits = 72 bytes (5 quadros de 16 bytes)

def converter_imagem_para_hex(caminho_imagem):
    img = Image.open(caminho_imagem).convert("RGBA")
    
    # Cria um fundo branco (necessário porque emojis PNG têm fundo transparente)
    fundo_branco = Image.new("RGBA", img.size, (255, 255, 255, 255))
    img_combinada = Image.alpha_composite(fundo_branco, img)
    
    # Redimensiona para 24x24 e converte para Preto e Branco puro (1 bit por pixel)
    img_pb = img_combinada.convert("L").resize((TAMANHO, TAMANHO))
    img_1bit = img_pb.point(lambda pixel: 0 if pixel < 180 else 1, mode='1')

    # Empacota cada 8 pixels em 1 byte
    bits = list(img_1bit.getdata())
    bytes_empacotados = bytearray()
    for i in range(0, len(bits), 8):
        byte = 0
        for bit_idx in range(8):
            # Inverte (1 - bit) para que os traços escuros do desenho virem '1'
            pixel_escuro = 1 - bits[i + bit_idx]
            byte = (byte << 1) | pixel_escuro
        bytes_empacotados.append(byte)

    return bytes_empacotados.hex().upper()

# Conecta ao ESP32
esp32 = serial.Serial(PORTA_USB, VELOCIDADE, timeout=1)
time.sleep(2) # Aguarda o ESP32 reiniciar ao abrir a porta serial

print("=== CONTROLADOR PYTHON DO TRANSMISSOR RF ===")
print("Comandos:")
print("  - Digite o nome do arquivo na pasta 'emojis' (ex: terra.png)")
print("  - Digite 'erro' para ativar a simulacao de falha no proximo quadro")
print("  - Ou digite qualquer texto normal para mandar ao LCD\n")

while True:
    comando = input("Digite arquivo/texto/comando: ").strip()
    if not comando:
        continue

    if comando.lower() == "erro":
        esp32.write(b"/erro\n")
    elif comando.lower().endswith((".png", ".jpg", ".jpeg")):
        caminho = os.path.join("emojis", comando)
        if os.path.exists(caminho):
            hex_dados = converter_imagem_para_hex(caminho)
            mensagem = f"IMG_HEX:{hex_dados}\n"
            print(f"[PYTHON] Imagem '{comando}' convertida em {len(hex_dados)//2} bytes. Enviando ao ESP32...")
            esp32.write(mensagem.encode("utf-8"))
        else:
            print(f"[ERRO] Arquivo '{caminho}' nao encontrado!")
    else:
        # Envia texto comum para aparecer no LCD
        esp32.write((comando + "\n").encode("utf-8"))

    # Mostra na tela os logs de retorno do ESP32 durante 4 segundos
    inicio = time.time()
    while time.time() - inicio < 4:
        if esp32.in_waiting > 0:
            linha = esp32.readline().decode("utf-8", errors="ignore").strip()
            if linha:
                print("  " + linha)
