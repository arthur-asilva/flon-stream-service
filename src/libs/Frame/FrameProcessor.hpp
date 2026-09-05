#pragma once

#include "Common.hpp"

#include "RecordingManager.hpp"
#include "ImageProcessor.hpp"

class FrameProcessor
{
public:

    FrameProcessor(
        RecordingManager& recordingManager,
        ImageProcessor& imageProcessor
    );

    cv::Mat process(
        const cv::Mat& frame,
        int wellsCount
    );

private:

    RecordingManager& recordingManager_;

    ImageProcessor& imageProcessor_;
};
