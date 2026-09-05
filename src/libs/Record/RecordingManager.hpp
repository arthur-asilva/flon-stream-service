#pragma once

#include "Common.hpp"

#include "WellDetection.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

class RecordingManager
{
public:

    RecordingManager();

    // id deve ser uma string apenas de digitos (validacao
    // feita em MjpegServer). Cria a pasta records/<id>/ e
    // salva sample.avi + coordinates.csv dentro dela.
    void start(
        const std::string& id,
        int durationSeconds
    );

    void stop();

    // Grava um frame de video e, se houver deteccoes, uma
    // linha correspondente em coordinates.csv. So tem
    // efeito enquanto isRecording() for true.
    void process(
        const cv::Mat& frame,
        const std::vector<WellDetection>& detections
    );

    bool isRecording() const;

private:

    // Executa a parada assumindo que mutex_ ja esta
    // travado pelo chamador (evita deadlock quando
    // process() precisa parar a gravacao internamente).
    void stopLocked();

    // Escreve o cabecalho do CSV (frame_index,x<id>,y<id>,...)
    // usando a ordem/identidade das deteccoes da primeira
    // linha gravada. So executa uma vez por gravacao.
    void writeCsvHeaderLocked(
        const std::vector<WellDetection>& detections
    );

    mutable std::mutex mutex_;

    bool recording_;

    std::string videoFileName_;

    cv::VideoWriter writer_;

    std::ofstream csvFile_;

    bool csvHeaderWritten_ = false;

    long long frameIndex_ = 0;

    std::chrono::steady_clock::time_point endTime_;
};
