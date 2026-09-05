#pragma once

#include "Common.hpp"

#include "WellMasker.hpp"
#include "WellDetection.hpp"

#include <unordered_map>
#include <vector>

class ImageProcessor
{
public:

    explicit ImageProcessor(WellMasker& wellMasker);

    // detections e preenchido com uma entrada por poco do
    // layout atual (mesma ordem de layout->wells), sempre
    // que houver um layout carregado para wellsCount. Se
    // nao houver layout, detections fica vazio.
    cv::Mat process(
        const cv::Mat& frame,
        int wellsCount,
        std::vector<WellDetection>& detections
    );

private:

    // Estado de rastreio de um poco individual: guarda o
    // recorte (ROI) em tons de cinza do frame anterior
    // DAQUELE MESMO poco, para comparacao quadro-a-quadro.
    struct WellState
    {
        cv::Mat previousRoi;

        bool hasPrevious = false;

        // Media (EMA) da intensidade media dos blobs ja
        // aceitos para este poco. Usada para rejeitar uma
        // nova deteccao cujo valor medio destoa muito do
        // historico (ex: pegar a cauda, mais fraca/mais
        // fina no diff, em vez da cabeca).
        double avgIntensityMean = 0.0;

        bool hasIntensityBaseline = false;
    };

    WellMasker& wellMasker_;

    // Um estado por poco (chave = Well::id).
    std::unordered_map<int, WellState> wellStates_;

    // Detecta troca de wells_count para descartar
    // baselines antigas (ROIs de outra configuracao).
    int lastWellsCount_;

    static constexpr int kAreaMin = 5;

    static constexpr double kThresholdFloor = 9.0;

    // Tolerancia relativa (fracao) entre o valor medio do
    // blob atual e a media historica do poco. Ex: 0.4 =
    // aceita deteccoes entre 60% e 140% da media. Fora
    // dessa faixa, a deteccao e tratada como ruido/regiao
    // incompativel e descartada.
    static constexpr double kIntensityToleranceFrac = 0.4;

    // Peso da nova amostra na media movel exponencial da
    // intensidade media aceita por poco.
    static constexpr double kIntensityEmaAlpha = 0.2;
};
