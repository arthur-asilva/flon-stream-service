#include "Common.hpp"
#include "WellMasker.hpp"
#include "CameraCapture.hpp"
#include "FrameProcessor.hpp"
#include "RecordingManager.hpp"
#include "ImageProcessor.hpp"
#include "MjpegServer.hpp"
#include "FrameStore.hpp"

int main()
{
#ifdef _WIN32

    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr
            << "Falha ao inicializar o Winsock.\n";

        return -1;
    }

#endif

    std::atomic<bool> running(true);

    FrameStore frameStore;

    WellMasker wellMasker;

    if (!wellMasker.initialize())
    {
        std::cerr
            << "Erro ao inicializar WellMasker.\n";

        return -1;
    }

    RecordingManager recordingManager;

    ImageProcessor imageProcessor(wellMasker);

    FrameProcessor frameProcessor(
        recordingManager,
        imageProcessor
    );

    CameraCapture camera(
        frameStore,
        wellMasker,
        frameProcessor,
        running
    );

    MjpegServer server(
        frameStore,
        camera,
        recordingManager,
        running,
        3000
    );

    std::cout
        << "Iniciando servidor de vídeo...\n";

    std::cout
        << "Pressione Ctrl+C para encerrar.\n\n";

    std::thread captureThread(
        &CameraCapture::run,
        &camera
    );

    std::thread serverThread(
        &MjpegServer::run,
        &server
    );

    captureThread.join();

    serverThread.join();

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
