import serial
from PIL import Image

# === CONFIGURAÇÃO ===
PORTA_USB = "COM4"  # Troque pela porta COM do ESP32 Receptor
VELOCIDADE = 115200
TAMANHO = 24        # Mesmo tamanho definido no transmissor (24x24)

def reconstruir_e_mostrar_imagem(hex_str):
    dados_bytes = bytes.fromhex(hex_str)
    img = Image.new("1", (TAMANHO, TAMANHO))
    pixels = []

    print("\n=== RECONSTRUINDO EMOJI RECEBIDO VIA RADIO ===")
    for byte in dados_bytes:
        for bit_idx in range(7, -1, -1):
            bit = (byte >> bit_idx) & 0x01
            # 0 = pixel preto no Pillow, 1 = pixel branco
            pixels.append(0 if bit == 1 else 1)

    # Desenha uma prévia em ASCII direto no terminal
    for y in range(TAMANHO):
        linha_str = ""
        for x in range(TAMANHO):
            idx = y * TAMANHO + x
            if idx < len(pixels):
                linha_str += "██" if pixels[idx] == 0 else "  "
        print(linha_str)

    # Salva e abre a imagem ampliada (360x360 pixels) na tela do computador
    img.putdata(pixels[:TAMANHO*TAMANHO])
    img_ampliada = img.resize((360, 360), Image.NEAREST)
    img_ampliada.save("emoji_recebido.png")
    img_ampliada.show()
    print("=== IMAGEM SALVA COMO 'emoji_recebido.png' E EXIBIDA NA TELA ===\n")

esp32 = serial.Serial(PORTA_USB, VELOCIDADE, timeout=1)
print("=== MONITOR RECEPTOR PYTHON INICIADO (Aguardando dados do ESP32...) ===\n")

while True:
    if esp32.in_waiting > 0:
        linha = esp32.readline().decode("utf-8", errors="ignore").strip()
        if not linha:
            continue

        if linha.startswith("IMG_HEX:"):
            hex_recebido = linha.replace("IMG_HEX:", "")
            reconstruir_e_mostrar_imagem(hex_recebido)
        else:
            print(linha)