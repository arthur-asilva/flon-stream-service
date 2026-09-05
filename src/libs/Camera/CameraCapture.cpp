#include "CameraCapture.hpp"

#include "Logger.hpp"

namespace
{
// Resolucao canonica do pipeline: precisa bater com
// os layouts de wells_*.json e com o cv::Size fixo
// usado pelo RecordingManager::start().
constexpr int kFrameWidth = 1024;
constexpr int kFrameHeight = 690;

// ROIs de 4 pontos por camera, calibradas com o tune_camera
// (coordenadas NATIVAS de cada camera, antes de qualquer
// correcao). Ordem dos pontos: superior-esquerdo, inferior-
// esquerdo, inferior-direito, superior-direito.
//
// Camera 0 = metade superior do frame final.
const std::array<cv::Point2f, 4> kRoiTop = {
    cv::Point2f(123.0f, 357.0f),
    cv::Point2f(120.0f, 1222.0f),
    cv::Point2f(2668.0f, 1235.0f),
    cv::Point2f(2676.0f, 373.0f)
};

// Camera 1 = metade inferior do frame final.
const std::array<cv::Point2f, 4> kRoiBottom = {
    cv::Point2f(102.0f, 887.0f),
    cv::Point2f(87.0f, 1755.0f),
    cv::Point2f(2653.0f, 1770.0f),
    cv::Point2f(2658.0f, 908.0f)
};
}

CameraCapture::CameraCapture(
    FrameStore& frameStore,
    WellMasker& wellMasker,
    FrameProcessor& frameProcessor,
    std::atomic<bool>& running
)
:
frameStore_(frameStore),
wellMasker_(wellMasker),
frameProcessor_(frameProcessor),
running_(running),
wellsCount_(0)
{
}

void CameraCapture::setWellsCount(
    int wellsCount
)
{
    wellsCount_ = wellsCount;
}

void CameraCapture::cameraCaptureLoop(
    GalaxyCamera& camera,
    CameraSlot& slot
)
{
    while(running_)
    {
        // Mat NOVO a cada iteracao (nao reaproveitado): assim
        // grab() sempre aloca um buffer novo, em vez de escrever
        // por cima de um frame que o loop principal pode estar
        // lendo naquele instante atraves do slot.
        cv::Mat frame;

        if(!camera.grab(frame))
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(5));

            continue;
        }

        std::lock_guard<std::mutex> lock(slot.mutex);

        slot.latestFrame = frame;
        slot.hasFrame = true;
    }
}

void CameraCapture::run()
{
    //----------------------------------------------------
    // Fontes de video: duas cameras Galaxy. Cada uma ja
    // devolve o frame corrigido (demosaic + perspectiva +
    // gama) no tamanho de metade da altura canonica.
    //----------------------------------------------------

    const cv::Size halfSize(kFrameWidth, kFrameHeight / 2);

    // A camera superior (indice 0) esta montada fisicamente de
    // cabeca para baixo - rotate180=true corrige isso dentro da
    // propria matriz de perspectiva, sem custo extra por frame.
    if(
        !cameraTop_.open(0, kRoiTop, halfSize, true) ||
        !cameraBottom_.open(1, kRoiBottom, halfSize, false)
    )
    {
        Logger::instance().logError(
            "[CAPTURE] Nao foi possivel abrir as duas cameras Galaxy."
        );

        cameraTop_.close();
        cameraBottom_.close();

        running_ = false;
        return;
    }

    //----------------------------------------------------
    // Uma thread dedicada por camera: o demosaic + warp de
    // cada uma roda em paralelo de verdade, em vez de uma
    // esperar a outra terminar na mesma thread.
    //----------------------------------------------------

    std::thread topThread(
        &CameraCapture::cameraCaptureLoop,
        this,
        std::ref(cameraTop_),
        std::ref(topSlot_)
    );

    std::thread bottomThread(
        &CameraCapture::cameraCaptureLoop,
        this,
        std::ref(cameraBottom_),
        std::ref(bottomSlot_)
    );

    //----------------------------------------------------

    cv::Mat topFrame;
    cv::Mat bottomFrame;
    cv::Mat frame;

    std::vector<int> jpegParams =
    {
        cv::IMWRITE_JPEG_QUALITY,
        85
    };

    std::vector<uchar> jpegBuffer;

    //----------------------------------------------------

    while(running_)
    {
        //--------------------------------------------
        // Le o frame mais recente publicado por cada
        // thread de captura (nao bloqueia esperando a
        // camera - so olha o que ja esta disponivel).
        //--------------------------------------------

        bool gotTop = false;
        bool gotBottom = false;

        {
            std::lock_guard<std::mutex> lock(topSlot_.mutex);

            if(topSlot_.hasFrame)
            {
                topFrame = topSlot_.latestFrame;
                gotTop = true;
            }
        }

        {
            std::lock_guard<std::mutex> lock(bottomSlot_.mutex);

            if(bottomSlot_.hasFrame)
            {
                bottomFrame = bottomSlot_.latestFrame;
                gotBottom = true;
            }
        }

        if(!gotTop || !gotBottom)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(5));

            continue;
        }

        //--------------------------------------------
        // Une as duas cameras verticalmente (superior
        // em cima da inferior).
        //--------------------------------------------

        cv::vconcat(
            topFrame,
            bottomFrame,
            frame
        );

        //--------------------------------------------
        // Padroniza resolucao
        //--------------------------------------------
        // Trava de seguranca: normalmente nao dispara, ja
        // que cada GalaxyCamera devolve exatamente metade
        // de kFrameHeight, e vconcat soma as duas metades.

        if(
            frame.cols != kFrameWidth ||
            frame.rows != kFrameHeight
        )
        {
            cv::resize(
                frame,
                frame,
                cv::Size(kFrameWidth, kFrameHeight)
            );
        }

        //--------------------------------------------
        // Leitura atomica da configuracao atual
        //--------------------------------------------

        const int wellsCount = wellsCount_;

        //--------------------------------------------
        // Mascara dos pocos
        //--------------------------------------------

        if(!wellMasker_.apply(
            frame,
            wellsCount
        ))
        {
            Logger::instance().logError(
                "[MASCARA] Erro ao aplicar mascara."
            );
        }

        //--------------------------------------------
        // Pipeline de processamento
        //--------------------------------------------

        cv::Mat processedFrame =
            frameProcessor_.process(
                frame,
                wellsCount
            );

        //--------------------------------------------
        // Compressao JPEG
        //--------------------------------------------

        jpegBuffer.clear();

        if(!cv::imencode(
            ".jpg",
            processedFrame,
            jpegBuffer,
            jpegParams
        ))
        {
            continue;
        }

        //--------------------------------------------
        // Publica para o servidor HTTP
        //--------------------------------------------

        frameStore_.update(
            std::move(jpegBuffer)
        );
    }

    topThread.join();
    bottomThread.join();

    cameraTop_.close();
    cameraBottom_.close();
}
