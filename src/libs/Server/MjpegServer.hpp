#pragma once

#include "Common.hpp"

#include "FrameStore.hpp"
#include "CameraCapture.hpp"
#include "RecordingManager.hpp"

class MjpegServer
{
public:

    MjpegServer(
        FrameStore& frameStore,
        CameraCapture& cameraCapture,
        RecordingManager& recordingManager,
        std::atomic<bool>& running,
        int port
    );

    void run();

private:

    struct StreamParams
    {
        std::string id;

        int durationSeconds = 0;

        int wellsCount = 0;
    };

    FrameStore& frameStore_;

    CameraCapture& cameraCapture_;

    RecordingManager& recordingManager_;

    std::atomic<bool>& running_;

    int port_;

    StreamParams getStreamParams(
        const std::string& request
    ) const;

    int getQueryInt(
        const std::string& request,
        const std::string& parameterName
    ) const;

    std::string getQueryString(
        const std::string& request,
        const std::string& parameterName
    ) const;

    void clientThread(
        int clientFd
    );
};
