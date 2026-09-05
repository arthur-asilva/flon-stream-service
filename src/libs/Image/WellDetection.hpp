#pragma once

// Deteccao de um poco em um frame especifico. Usado para
// propagar as coordenadas do ImageProcessor ate o
// RecordingManager, que grava as posicoes em
// coordinates.csv durante a gravacao. Sem dependencia de
// OpenCV/WellMasker de proposito, para nao acoplar quem
// so precisa deste dado a classe inteira do ImageProcessor.
struct WellDetection
{
    int wellId = 0;

    // Se false, nao houve movimento detectado neste poco
    // neste frame - x/y devem ser ignorados (viram celula
    // vazia no CSV).
    bool detected = false;

    double x = 0.0;

    double y = 0.0;
};
