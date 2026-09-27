#pragma once

#include <opencv2/opencv.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "WellDetection.hpp"

class RecordingManager
{
public:

    RecordingManager();
    
    void start(
        const std::string& id,
        int durationSeconds
    );

    void stop();

    void process(
        const cv::Mat& frame,
        const std::vector<WellDetection>& detections
    );

    bool isRecording() const;

private:

    void stopLocked();

    void writeCsvHeaderLocked(const std::vector<WellDetection>& detections);

    void writeCsvRowLocked(const std::vector<WellDetection>& detections);

    mutable std::mutex mutex_;

    bool recording_;
    bool csvHeaderWritten_;

    int id_;

    std::string sessionDir_;
    std::string videoFileName_;
    std::string csvFileName_;

    cv::VideoWriter writer_;
    std::ofstream csvFile_;

    long frameIndex_;

    std::chrono::steady_clock::time_point endTime_;

    // Paceamento de frames: garante que o video gravado tenha
    // exatamente kTargetFps frames por segundo REAL, independente
    // da taxa real de entrega de frames das cameras. Sem isso, se a
    // camera entregar frames mais rapido que o fps declarado no
    // VideoWriter, o video final "dura" mais do que o tempo real de
    // gravacao quando reproduzido (frames_escritos / fps_declarado).
    std::chrono::steady_clock::time_point recordStart_;
    std::chrono::steady_clock::time_point nextFrameDue_;

    static constexpr double kTargetFps = 30.0;
    static constexpr int kFrameWidth = 1024;
    static constexpr int kFrameHeight = 690;
};