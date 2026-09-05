#include "FrameProcessor.hpp"

FrameProcessor::FrameProcessor(
    RecordingManager& recordingManager,
    ImageProcessor& imageProcessor
)
:
recordingManager_(recordingManager),
imageProcessor_(imageProcessor)
{
}

cv::Mat FrameProcessor::process(
    const cv::Mat& frame,
    int wellsCount
)
{
    //----------------------------------------------------
    // Processa a imagem
    //----------------------------------------------------

    std::vector<WellDetection> detections;

    cv::Mat processedFrame =
        imageProcessor_.process(
            frame,
            wellsCount,
            detections
        );

    //----------------------------------------------------
    // Envia o frame processado e as coordenadas do
    // frame para gravação (video + coordinates.csv)
    //----------------------------------------------------

    recordingManager_.process(
        processedFrame,
        detections
    );

    //----------------------------------------------------
    // Retorna para CameraCapture
    //----------------------------------------------------

    return processedFrame;
}
