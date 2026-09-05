#pragma once

#include "Common.hpp"

#include "FrameStore.hpp"
#include "WellMasker.hpp"
#include "FrameProcessor.hpp"
#include "GalaxyCamera.hpp"

class CameraCapture
{
public:

    CameraCapture(
        FrameStore& frameStore,
        WellMasker& wellMasker,
        FrameProcessor& frameProcessor,
        std::atomic<bool>& running
    );

    void run();

    void setWellsCount(int wellsCount);

private:

    // Guarda o frame mais recente de UMA camera, protegido por
    // mutex - permite que a thread de captura daquela camera
    // publique frames enquanto a thread principal le o mais
    // recente, sem bloquear uma a outra por muito tempo.
    struct CameraSlot
    {
        mutable std::mutex mutex;

        cv::Mat latestFrame;

        bool hasFrame = false;
    };

    // Roda em uma thread dedicada por camera: fica chamando
    // GalaxyCamera::grab() (que ja inclui demosaic, warp e gama)
    // em loop e publicando o resultado no slot correspondente.
    // Rodar as duas cameras em threads separadas permite que o
    // trabalho pesado de cada uma aconteca em paralelo de verdade,
    // em vez de uma esperar a outra terminar.
    void cameraCaptureLoop(
        GalaxyCamera& camera,
        CameraSlot& slot
    );

    FrameStore& frameStore_;

    WellMasker& wellMasker_;

    FrameProcessor& frameProcessor_;

    std::atomic<bool>& running_;

    std::atomic<int> wellsCount_;

    // Duas cameras Galaxy (indices 0 e 1). Cada uma ja devolve o
    // frame com demosaic, gain, gama e correcao de perspectiva
    // aplicados (ver GalaxyCamera::grab()). A imagem final do
    // stream e a uniao vertical das duas (superior em cima da
    // inferior).
    GalaxyCamera cameraTop_;

    GalaxyCamera cameraBottom_;

    CameraSlot topSlot_;

    CameraSlot bottomSlot_;
};
