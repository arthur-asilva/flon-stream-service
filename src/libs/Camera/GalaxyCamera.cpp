#include "GalaxyCamera.hpp"

#include "Logger.hpp"

#include <GxIAPI.h>

#include <cmath>

const int GalaxyCamera::kBayerConversionCode = cv::COLOR_BayerBG2BGR;

namespace
{
// GXInitLib()/GXCloseLib() sao globais ao processo, nao por
// camera. Referencia contada para inicializar na abertura da
// primeira camera e finalizar quando a ultima for fechada.
int g_libRefCount = 0;

// So reenumeramos quando a lib e inicializada do zero (primeira
// camera a abrir) - chamar GXUpdateDeviceList de novo com uma
// camera ja aberta/streamando pode invalidar handles ou gerar
// conflito de acesso em algumas versoes do SDK.
uint32_t g_deviceCount = 0;

GX_DEV_HANDLE toHandle(void* raw)
{
    return static_cast<GX_DEV_HANDLE>(raw);
}

// Formatos Bayer 8-bit padrao GenICam/PFNC. Qualquer outro valor
// e tratado como Mono8 (passthrough, sem demosaic).
bool isBayerPixelFormat(int64_t pixelFormat)
{
    switch (pixelFormat)
    {
        case 0x01080008: // BayerGR8
        case 0x01080009: // BayerRG8
        case 0x0108000A: // BayerGB8
        case 0x0108000B: // BayerBG8
            return true;
        default:
            return false;
    }
}
}

GalaxyCamera::GalaxyCamera()
:
handle_(nullptr),
streaming_(false),
isBayer_(false)
{
}

GalaxyCamera::~GalaxyCamera()
{
    close();
}

bool GalaxyCamera::open(
    int deviceIndex,
    const std::array<cv::Point2f, 4>& roiPoints,
    cv::Size outputSize,
    bool rotate180
)
{
    outputSize_ = outputSize;

    //----------------------------------------------------
    // Matriz de correcao de perspectiva: os 4 pontos da ROI
    // (superior-esquerdo, inferior-esquerdo, inferior-direito,
    // superior-direito) mapeados para um retangulo perfeito de
    // outputSize.
    //
    // Se rotate180=true, cada ponto de origem e mapeado para o
    // canto OPOSTO do que iria normalmente - equivale a rodar a
    // imagem de saida 180 graus, mas embutido na mesma matriz
    // que ja seria calculada de qualquer forma (custo zero por
    // frame, ao contrario de um cv::rotate() separado).
    //----------------------------------------------------

    const cv::Point2f corners[4] = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(0.0f, static_cast<float>(outputSize.height - 1)),
        cv::Point2f(static_cast<float>(outputSize.width - 1),
                    static_cast<float>(outputSize.height - 1)),
        cv::Point2f(static_cast<float>(outputSize.width - 1), 0.0f)
    };

    cv::Point2f dstPoints[4];

    if (rotate180)
    {
        dstPoints[0] = corners[2];
        dstPoints[1] = corners[3];
        dstPoints[2] = corners[0];
        dstPoints[3] = corners[1];
    }
    else
    {
        dstPoints[0] = corners[0];
        dstPoints[1] = corners[1];
        dstPoints[2] = corners[2];
        dstPoints[3] = corners[3];
    }

    perspectiveMatrix_ = cv::getPerspectiveTransform(roiPoints.data(), dstPoints);

    //----------------------------------------------------
    // LUT de gama por software, calculado uma unica vez.
    //----------------------------------------------------

    gammaLut_.create(1, 256, CV_8U);

    uchar* lutPtr = gammaLut_.ptr();

    for (int i = 0; i < 256; i++)
    {
        const double normalized = static_cast<double>(i) / 255.0;
        const double corrected = std::pow(normalized, 1.0 / kGamma);

        lutPtr[i] = cv::saturate_cast<uchar>(corrected * 255.0);
    }

    //----------------------------------------------------
    // Abertura da camera.
    //----------------------------------------------------

    if (g_libRefCount == 0)
    {
        GXInitLib();

        GXUpdateDeviceList(&g_deviceCount, 1000);

        std::cout
            << "[GALAXY] "
            << g_deviceCount
            << " camera(s) detectada(s) na enumeracao.\n";
    }

    g_libRefCount++;

    if (g_deviceCount == 0)
    {
        Logger::instance().logError(
            "[GALAXY] Nenhuma camera encontrada na enumeracao."
        );

        g_libRefCount--;

        if (g_libRefCount == 0)
        {
            GXCloseLib();
        }

        return false;
    }

    if (static_cast<uint32_t>(deviceIndex) >= g_deviceCount)
    {
        Logger::instance().logError(
            "[GALAXY] Indice " + std::to_string(deviceIndex) +
            " pedido, mas so " + std::to_string(g_deviceCount) +
            " camera(s) detectada(s). Confira se ambas as cameras "
            "estao fisicamente conectadas."
        );

        g_libRefCount--;

        if (g_libRefCount == 0)
        {
            GXCloseLib();
        }

        return false;
    }

    GX_DEV_HANDLE handle = nullptr;

    // GXOpenDeviceByIndex espera indice comecando em 1.
    const GX_STATUS status = GXOpenDeviceByIndex(
        static_cast<uint32_t>(deviceIndex + 1),
        &handle
    );

    if (status != GX_STATUS_SUCCESS)
    {
        Logger::instance().logError(
            "[GALAXY] Falha ao abrir camera indice " +
            std::to_string(deviceIndex) + " (status " +
            std::to_string(status) + ")"
        );

        g_libRefCount--;

        if (g_libRefCount == 0)
        {
            GXCloseLib();
        }

        return false;
    }

    handle_ = handle;

    //----------------------------------------------------
    // Formato de pixel: decide se precisa demosaic.
    //----------------------------------------------------

    int64_t pixelFormat = 0;

    GXGetEnum(toHandle(handle_), GX_ENUM_PIXEL_FORMAT, &pixelFormat);

    isBayer_ = isBayerPixelFormat(pixelFormat);

    //----------------------------------------------------
    // Gain (calibrado com o tune_camera). Ignora o status de
    // retorno aqui: se a camera nao aceitar exatamente este
    // valor, ainda assim tenta continuar com o default de fabrica.
    //----------------------------------------------------

    GXSetFloat(toHandle(handle_), GX_FLOAT_GAIN, kGain);

    //----------------------------------------------------
    // Modo de aquisicao continuo, sem trigger externo.
    //----------------------------------------------------

    GXSetEnum(toHandle(handle_), GX_ENUM_ACQUISITION_MODE, GX_ACQ_MODE_CONTINUOUS);
    GXSetEnum(toHandle(handle_), GX_ENUM_TRIGGER_MODE, GX_TRIGGER_MODE_OFF);

    const GX_STATUS streamStatus = GXStreamOn(toHandle(handle_));

    if (streamStatus != GX_STATUS_SUCCESS)
    {
        Logger::instance().logError(
            "[GALAXY] Falha ao iniciar stream da camera indice " +
            std::to_string(deviceIndex) + " (status " +
            std::to_string(streamStatus) + ")"
        );

        GXCloseDevice(toHandle(handle_));

        handle_ = nullptr;

        g_libRefCount--;

        if (g_libRefCount == 0)
        {
            GXCloseLib();
        }

        return false;
    }

    streaming_ = true;

    std::cout
        << "[GALAXY] Camera indice "
        << deviceIndex
        << " aberta e streaming ("
        << (isBayer_ ? "Bayer, demosaic ativo" : "Mono8")
        << ").\n";

    return true;
}

bool GalaxyCamera::grab(
    cv::Mat& outFrame,
    unsigned int timeoutMs
)
{
    if (!streaming_ || handle_ == nullptr)
    {
        return false;
    }

    PGX_FRAME_BUFFER frameBuffer = nullptr;

    const GX_STATUS status = GXDQBuf(
        toHandle(handle_),
        &frameBuffer,
        timeoutMs
    );

    if (status != GX_STATUS_SUCCESS || frameBuffer == nullptr)
    {
        return false;
    }

    if (frameBuffer->nStatus != GX_FRAME_STATUS_SUCCESS)
    {
        GXQBuf(toHandle(handle_), frameBuffer);

        return false;
    }

    const cv::Mat raw(
        frameBuffer->nHeight,
        frameBuffer->nWidth,
        CV_8UC1,
        frameBuffer->pImgBuf
    );

    cv::Mat colorFrame;

    if (isBayer_)
    {
        cv::cvtColor(raw, colorFrame, kBayerConversionCode);
    }
    else
    {
        cv::cvtColor(raw, colorFrame, cv::COLOR_GRAY2BGR);
    }

    // Correcao de perspectiva PRIMEIRO: reduz de resolucao nativa
    // (varios megapixels) para outputSize (bem menor) antes do
    // LUT de gama rodar - gama e por-pixel, entao a ordem nao
    // muda o resultado, so o custo (aplicar depois do warp e bem
    // mais barato que aplicar antes, na imagem cheia).
    cv::Mat warped;

    cv::warpPerspective(
        colorFrame,
        warped,
        perspectiveMatrix_,
        outputSize_
    );

    cv::LUT(warped, gammaLut_, outFrame);

    GXQBuf(toHandle(handle_), frameBuffer);

    return true;
}

void GalaxyCamera::close()
{
    if (handle_ == nullptr)
    {
        return;
    }

    if (streaming_)
    {
        GXStreamOff(toHandle(handle_));

        streaming_ = false;
    }

    GXCloseDevice(toHandle(handle_));

    handle_ = nullptr;

    g_libRefCount--;

    if (g_libRefCount == 0)
    {
        GXCloseLib();
    }
}

bool GalaxyCamera::isOpen() const
{
    return handle_ != nullptr;
}
