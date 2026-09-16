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

        // true se a URL trouxe a chave "wells_count" (mesmo que
        // com valor invalido/zero) - distingue "parametro ausente"
        // (nao mexe na configuracao atual - protege contra request
        // perdido, ex: favicon.ico) de "presente mas invalido/0"
        // (pedido explicito de ver sem mascara).
        bool wellsCountProvided = false;
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

    // Verifica se a URL contem a chave do parametro, independente
    // do valor ser valido ou nao vazio - diferente de getQueryInt/
    // getQueryString, que nao distinguem "ausente" de "invalido".
    bool hasQueryParam(
        const std::string& request,
        const std::string& parameterName
    ) const;

    void clientThread(
        int clientFd
    );
};
