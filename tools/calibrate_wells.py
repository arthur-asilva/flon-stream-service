#!/usr/bin/env python3
"""
Calibrador de coordenadas dos pocos (v2 - com ajuste fino por teclado).

Abre o primeiro frame do sample.avi (com o MESMO flip que o
CameraCapture.cpp aplica) numa janela.

Controles:
    Clique perto de um poco existente -> seleciona ele (fica amarelo)
    Clique em area vazia              -> cria um poco novo ali e seleciona
    Setas (up/down/left/right)        -> move o poco selecionado
    [ / ]                             -> diminui/aumenta o passo do deslocamento
    + / -                             -> diminui/aumenta o raio (visual)
    n / p                             -> seleciona o proximo/anterior poco da lista
    z ou Backspace                    -> remove o poco selecionado
    s                                  -> salva o JSON e sai
    q ou ESC                          -> sai sem salvar

Uso:
    python3 calibrate_wells.py <sample.avi> <raio_inicial> <saida.json>

Exemplo:
    python3 calibrate_wells.py \
        /home/arthur/ZSafe/src/records/sample.avi \
        30 \
        /home/arthur/ZSafe/src/config/wells_96.json
"""

import sys
import json
import cv2


# Codigos de tecla para as setas. Variam por plataforma/backend do
# OpenCV highgui - cobrimos Linux/X11 (mais comum no Debian) e
# Windows, para o script funcionar nos dois ambientes.
ARROW_CODES = {
    "left":  {65361, 2424832, 81},
    "right": {65363, 2555904, 83},
    "up":    {65362, 2490368, 82},
    "down":  {65364, 2621440, 84},
}


def classify_arrow(key):
    for direction, codes in ARROW_CODES.items():
        if key in codes:
            return direction
    return None


def main():
    if len(sys.argv) < 4:
        print("Uso: python3 calibrate_wells.py <sample.avi> <raio_inicial> <saida.json>")
        sys.exit(1)

    video_path = sys.argv[1]
    radius = int(sys.argv[2])
    out_path = sys.argv[3]

    cap = cv2.VideoCapture(video_path)
    ok, frame = cap.read()
    cap.release()

    if not ok:
        print(f"Nao consegui ler um frame de {video_path}")
        sys.exit(1)

    # Mesmo flip que o CameraCapture.cpp aplica antes de mascarar -
    # as coordenadas precisam ser calibradas NESTE referencial.
    frame = cv2.flip(frame, 1)

    height, width = frame.shape[:2]
    print(f"Frame: {width}x{height}")

    wells = []
    state = {"radius": radius, "step": 1, "selected": -1}

    window_name = "Calibracao de pocos - clique para marcar/selecionar"

    def renumber():
        # Mantem well["id"] sempre igual a posicao na lista, para o
        # JSON final ficar com IDs sequenciais e sem furos.
        for i, well in enumerate(wells):
            well["id"] = i

    def redraw():
        img = frame.copy()

        for i, well in enumerate(wells):
            is_selected = (i == state["selected"])
            color = (0, 255, 255) if is_selected else (0, 255, 0)
            thickness = 1

            cv2.circle(img, (well["x"], well["y"]), state["radius"], color, thickness)
            cv2.circle(img, (well["x"], well["y"]), 2, (0, 0, 255), -1)
            cv2.putText(img, str(well["id"]), (well["x"] - 8, well["y"] - state["radius"] - 6),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)

        status = (f"raio={state['radius']}  passo={state['step']}px  "
                  f"pocos={len(wells)}  selecionado={state['selected']}")
        help_line = "[click=marca/seleciona] [setas=move] [n/p=troca] [z=remove] [s=salva] [q=sai]"

        cv2.putText(img, status, (10, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)
        cv2.putText(img, help_line, (10, 42), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)

        cv2.imshow(window_name, img)

    def closest_well(x, y):
        best_index = -1
        best_dist_sq = None
        for i, well in enumerate(wells):
            dist_sq = (well["x"] - x) ** 2 + (well["y"] - y) ** 2
            if best_dist_sq is None or dist_sq < best_dist_sq:
                best_dist_sq = dist_sq
                best_index = i
        # So considera "perto o suficiente" se estiver dentro do raio
        if best_index >= 0 and best_dist_sq <= state["radius"] ** 2:
            return best_index
        return -1

    def on_mouse(event, x, y, flags, param):
        if event != cv2.EVENT_LBUTTONDOWN:
            return

        existing = closest_well(x, y)

        if existing >= 0:
            state["selected"] = existing
        else:
            wells.append({"id": len(wells), "x": x, "y": y})
            state["selected"] = len(wells) - 1

        redraw()

    cv2.namedWindow(window_name)
    cv2.setMouseCallback(window_name, on_mouse)
    redraw()

    while True:
        key = cv2.waitKeyEx(20)

        if key == -1:
            continue

        arrow = classify_arrow(key)

        if arrow is not None:
            if state["selected"] >= 0:
                well = wells[state["selected"]]
                step = state["step"]

                if arrow == "left":
                    well["x"] -= step
                elif arrow == "right":
                    well["x"] += step
                elif arrow == "up":
                    well["y"] -= step
                elif arrow == "down":
                    well["y"] += step

                redraw()

            continue

        key_char = key & 0xFF

        if key_char in (ord('q'), 27):  # q ou ESC
            print("Saindo sem salvar.")
            break

        elif key_char in (ord('z'), 8):  # z ou backspace: remove o selecionado
            if state["selected"] >= 0:
                wells.pop(state["selected"])
                state["selected"] = min(state["selected"], len(wells) - 1)
                renumber()
                redraw()

        elif key_char == ord('n'):  # proximo poco
            if wells:
                state["selected"] = (state["selected"] + 1) % len(wells)
                redraw()

        elif key_char == ord('p'):  # poco anterior
            if wells:
                state["selected"] = (state["selected"] - 1) % len(wells)
                redraw()

        elif key_char == ord('['):  # passo menor (ajuste fino)
            state["step"] = max(1, state["step"] - 1)
            redraw()

        elif key_char == ord(']'):  # passo maior (ajuste rapido)
            state["step"] += 1
            redraw()

        elif key_char == ord('+'):
            state["radius"] += 1
            redraw()

        elif key_char == ord('-'):
            state["radius"] = max(1, state["radius"] - 1)
            redraw()

        elif key_char == ord('s'):
            renumber()
            layout = {
                "reference_width": width,
                "reference_height": height,
                "radius": state["radius"],
                "wells": wells,
            }
            with open(out_path, "w") as f:
                json.dump(layout, f, indent=3)
            print(f"Salvo: {out_path} ({len(wells)} pocos, raio {state['radius']})")
            break

        else:
            # Tecla nao reconhecida (provavelmente uma seta com
            # codigo diferente do esperado neste ambiente). Imprime
            # o codigo bruto para eu poder mapear certo.
            print(f"[debug] tecla nao reconhecida: key={key}  key_char={key_char}")

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
