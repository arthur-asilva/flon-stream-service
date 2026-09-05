#!/usr/bin/env python3
"""
Diagnostico de alinhamento da mascara dos pocos.

Le o primeiro frame do sample.avi, aplica o MESMO flip horizontal
que o CameraCapture.cpp faz, e desenha os circulos do wells_*.json
por cima (contorno apenas, sem mascarar nada) - assim da pra ver
visualmente se o circulo cai em cima do poco real ou desalinhado.

Uso:
    python3 check_mask_alignment.py <caminho_sample.avi> <caminho_wells.json> [saida.png]

Exemplo (no seu Debian):
    python3 check_mask_alignment.py \
        /home/arthur/ZSafe/src/records/sample.avi \
        /home/arthur/ZSafe/src/config/wells_96.json \
        /home/arthur/ZSafe/overlay_flip.png

O script tambem gera uma segunda imagem SEM o flip, para comparar
os dois cenarios lado a lado (overlay_sem_flip.png).
"""

import sys
import json
import cv2


def main():
    if len(sys.argv) < 3:
        print("Uso: python3 check_mask_alignment.py <sample.avi> <wells.json> [saida.png]")
        sys.exit(1)

    video_path = sys.argv[1]
    json_path = sys.argv[2]
    out_path = sys.argv[3] if len(sys.argv) > 3 else "overlay.png"

    cap = cv2.VideoCapture(video_path)
    ok, frame = cap.read()
    cap.release()

    if not ok:
        print(f"Nao consegui ler um frame de {video_path}")
        sys.exit(1)

    print(f"Frame lido: {frame.shape[1]}x{frame.shape[0]}")

    with open(json_path, "r") as f:
        layout = json.load(f)

    radius = layout["radius"]
    wells = layout["wells"]

    print(f"JSON: reference {layout['reference_width']}x{layout['reference_height']}, "
          f"raio {radius}, {len(wells)} pocos")

    def draw_overlay(base_frame, label):
        img = base_frame.copy()
        for well in wells:
            cv2.circle(img, (well["x"], well["y"]), radius, (0, 255, 0), 2)
            cv2.circle(img, (well["x"], well["y"]), 2, (0, 0, 255), -1)
        cv2.putText(img, label, (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                    0.8, (0, 255, 255), 2)
        return img

    # Cenario 1: COM flip (igual ao CameraCapture.cpp faz)
    flipped = cv2.flip(frame, 1)
    overlay_flip = draw_overlay(flipped, "COM flip (igual ao CameraCapture)")
    out_flip = out_path.replace(".png", "_com_flip.png")
    cv2.imwrite(out_flip, overlay_flip)
    print(f"Salvo: {out_flip}")

    # Cenario 2: SEM flip (frame cru)
    overlay_noflip = draw_overlay(frame, "SEM flip (frame cru)")
    out_noflip = out_path.replace(".png", "_sem_flip.png")
    cv2.imwrite(out_noflip, overlay_noflip)
    print(f"Salvo: {out_noflip}")

    print("\nCompare as duas imagens: em qual delas os circulos verdes")
    print("caem exatamente em cima dos pocos reais?")


if __name__ == "__main__":
    main()
