// Ajuste interativo de Gain / Gamma / ROI - NAO faz parte do
// projeto ZSafe, e uma ferramenta standalone de calibracao.
//
// Abre uma camera Galaxy por indice, mostra o stream ao vivo numa
// janela com largura fixa de 1200px (altura proporcional), com
// sliders para:
//   - Gain     -> parametro de hardware da propria camera
//   - Gamma    -> correcao por SOFTWARE (LUT), aplicada depois da
//                 captura - a camera nao expoe um GX_FLOAT_GAMMA
//                 utilizavel nesta unidade (range vazio), entao
//                 fazemos por conta propria
//   - Contrast -> so aparece se a camera reportar um range valido
//                 para GX_INT_CONTRAST_PARAM
//
// A camera e Bayer (confirmado via GX_ENUM_PIXEL_FORMAT), entao o
// programa decodifica (demosaic) antes de exibir. BayerBG2BGR ja
// vem como ordem padrao (confirmada visualmente); aperte 'b' para
// alternar caso a outra camera precise de uma ordem diferente.
//
// Alem disso, permite marcar uma ROI de 4 pontos (util quando o
// objeto/poco esta um pouco torto e um retangulo alinhado aos
// eixos nao serve) - clique uma vez por ponto, na ordem que fizer
// sentido para voce (ex: sentido horario a partir do canto
// superior esquerdo). Os pontos sao impressos em coordenadas
// NATIVAS da camera (nao nas coordenadas reduzidas da tela).
//
// Controles:
//   Clique          -> marca o proximo ponto da ROI (ate 4)
//   z               -> desfaz o ultimo ponto marcado
//   r               -> limpa todos os pontos
//   Sliders         -> ajustam Gain/Gamma(/Contrast) ao vivo
//   b               -> alterna a ordem do padrao Bayer
//   s               -> imprime todos os valores atuais no terminal
//   q ou ESC        -> sai
//
// Compilar: ver CMakeLists.txt do projeto (alvo tune_camera).
//
// Rodar (0 ou 1 = indice da camera que voce quer calibrar - marque
// a ROI de uma camera de cada vez, rodando o programa duas vezes):
//
//   ./tune_camera 0

#include <GxIAPI.h>

#include <opencv2/opencv.hpp>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

namespace
{

// --------------------------------------------------------------
// Estado global simples - aceitavel aqui porque e um utilitario
// standalone de calibracao, nao faz parte do projeto principal.
// --------------------------------------------------------------

GX_DEV_HANDLE g_handle = nullptr;

GX_FLOAT_RANGE g_gainRange{};
GX_FLOAT_RANGE g_gammaRange{};
GX_INT_RANGE   g_contrastRange{};

bool g_gainSupported = false;
bool g_hardwareGammaSupported = false;
bool g_contrastSupported = false;

const int kSliderSteps = 1000;

const int64_t kMaxReasonableIntSpan = 1000000;

// --------------------------------------------------------------
// Gama por software: range fixo (nao depende da camera), com
// 1.0 = sem correcao (imagem original).
// --------------------------------------------------------------

const double kSoftGammaMin = 0.1;
const double kSoftGammaMax = 3.0;

double g_softGamma = 1.0;

cv::Mat g_gammaLut(1, 256, CV_8U);

void rebuildGammaLut()
{
    uchar* ptr = g_gammaLut.ptr();

    for (int i = 0; i < 256; i++)
    {
        const double normalized = static_cast<double>(i) / 255.0;
        const double corrected = std::pow(normalized, 1.0 / g_softGamma);

        ptr[i] = cv::saturate_cast<uchar>(corrected * 255.0);
    }
}

// --------------------------------------------------------------
// Demosaic Bayer - alternavel ao vivo com a tecla 'b', porque a
// ordem exata (RG/BG/GR/GB) as vezes fica trocada em relacao ao
// que o fabricante chama o formato.
// --------------------------------------------------------------

const int g_bayerCodes[] = {
    cv::COLOR_BayerBG2BGR,
    cv::COLOR_BayerRG2BGR,
    cv::COLOR_BayerGR2BGR,
    cv::COLOR_BayerGB2BGR,
};

const char* g_bayerNames[] = {
    "BayerBG2BGR",
    "BayerRG2BGR",
    "BayerGR2BGR",
    "BayerGB2BGR",
};

const int kBayerCodeCount = 4;

int g_bayerCodeIndex = 0;

bool g_isBayerCamera = false;

// --------------------------------------------------------------
// Exibicao: largura fixa de 1200px, altura proporcional. As
// coordenadas do mouse chegam no espaco da tela (reduzido) e
// precisam ser convertidas de volta para o espaco nativo da
// camera antes de salvar/imprimir a ROI.
// --------------------------------------------------------------

const int kDisplayWidth = 1200;

cv::Size g_nativeSize(0, 0);
double g_displayScale = 1.0;
bool g_nativeSizeKnown = false;

// --------------------------------------------------------------
// Selecao de ROI por 4 pontos (clique um de cada vez). Guardamos
// em coordenadas de tela (para desenhar) e nativas (para uso
// real no recorte/perspectiva) em paralelo, sempre em sincronia.
// --------------------------------------------------------------

const size_t kRoiPointCount = 4;

std::vector<cv::Point> g_roiPointsDisplay;
std::vector<cv::Point> g_roiPointsNative;

void printRoiPoints()
{
    if (g_roiPointsNative.empty())
    {
        std::printf("ROI: nenhum ponto marcado ainda.\n");
        return;
    }

    std::printf("ROI (coords nativas), %zu/%zu pontos:\n",
                 g_roiPointsNative.size(), kRoiPointCount);

    for (size_t i = 0; i < g_roiPointsNative.size(); i++)
    {
        std::printf("  ponto %zu: x=%d y=%d\n",
                     i + 1, g_roiPointsNative[i].x, g_roiPointsNative[i].y);
    }
}

void onMouse(int event, int x, int y, int, void*)
{
    if (event != cv::EVENT_LBUTTONDOWN)
    {
        return;
    }

    if (g_roiPointsDisplay.size() >= kRoiPointCount)
    {
        std::printf("Ja tem %zu pontos - aperte 'z' para desfazer o ultimo "
                     "ou 'r' para limpar tudo antes de marcar outro.\n",
                     kRoiPointCount);
        return;
    }

    g_roiPointsDisplay.push_back(cv::Point(x, y));

    const cv::Point nativePoint(
        static_cast<int>(x / g_displayScale),
        static_cast<int>(y / g_displayScale)
    );

    g_roiPointsNative.push_back(nativePoint);

    std::printf("Ponto %zu/%zu (coords nativas): x=%d y=%d\n",
                 g_roiPointsNative.size(), kRoiPointCount,
                 nativePoint.x, nativePoint.y);

    if (g_roiPointsDisplay.size() == kRoiPointCount)
    {
        std::printf("ROI completa com os 4 pontos.\n");
    }
}


// --------------------------------------------------------------
// Helpers de conversao slider (int 0..kSliderSteps) <-> valor real
// --------------------------------------------------------------

double sliderToFloat(int sliderPos, const GX_FLOAT_RANGE& range)
{
    const double frac = static_cast<double>(sliderPos) / kSliderSteps;
    return range.dMin + frac * (range.dMax - range.dMin);
}

int floatToSlider(double value, const GX_FLOAT_RANGE& range)
{
    if (range.dMax <= range.dMin)
    {
        return 0;
    }

    const double frac = (value - range.dMin) / (range.dMax - range.dMin);
    return static_cast<int>(frac * kSliderSteps + 0.5);
}

int64_t sliderToInt(int sliderPos, const GX_INT_RANGE& range)
{
    return range.nMin + sliderPos;
}

int intToSlider(int64_t value, const GX_INT_RANGE& range)
{
    return static_cast<int>(value - range.nMin);
}

bool isUsableFloatRange(const GX_FLOAT_RANGE& range)
{
    return range.dMax > range.dMin;
}

bool isUsableIntRange(const GX_INT_RANGE& range)
{
    if (range.nMax <= range.nMin)
    {
        return false;
    }

    const int64_t span = range.nMax - range.nMin;

    return span <= kMaxReasonableIntSpan;
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

// --------------------------------------------------------------
// Callbacks dos sliders
// --------------------------------------------------------------

void onGainChange(int pos, void*)
{
    const double value = sliderToFloat(pos, g_gainRange);
    GXSetFloat(g_handle, GX_FLOAT_GAIN, value);
}

void onSoftGammaChange(int pos, void*)
{
    const double frac = static_cast<double>(pos) / kSliderSteps;
    g_softGamma = kSoftGammaMin + frac * (kSoftGammaMax - kSoftGammaMin);
    rebuildGammaLut();
}

void onContrastChange(int pos, void*)
{
    const int64_t value = sliderToInt(pos, g_contrastRange);
    GXSetInt(g_handle, GX_INT_CONTRAST_PARAM, value);
}

void printCurrentValues()
{
    std::printf("\n=== Valores atuais ===\n");

    if (g_gainSupported)
    {
        double gain = 0.0;
        GXGetFloat(g_handle, GX_FLOAT_GAIN, &gain);
        std::printf("GX_FLOAT_GAIN            = %.4f\n", gain);
    }
    else
    {
        std::printf("GX_FLOAT_GAIN            = (nao suportado nesta camera)\n");
    }

    std::printf("Gamma (software, LUT)    = %.4f\n", g_softGamma);

    if (g_contrastSupported)
    {
        int64_t contrast = 0;
        GXGetInt(g_handle, GX_INT_CONTRAST_PARAM, &contrast);
        std::printf("GX_INT_CONTRAST_PARAM    = %lld\n",
                     static_cast<long long>(contrast));
    }
    else
    {
        std::printf("GX_INT_CONTRAST_PARAM    = (nao suportado nesta camera)\n");
    }

    if (g_isBayerCamera)
    {
        std::printf("Ordem Bayer escolhida    = %s\n", g_bayerNames[g_bayerCodeIndex]);
    }

    printRoiPoints();

    std::printf("=======================\n\n");
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "Uso: %s <indice_camera 0 ou 1>\n", argv[0]);
        return 1;
    }

    const int deviceIndex = std::atoi(argv[1]);

    GXInitLib();

    uint32_t deviceCount = 0;
    GXUpdateDeviceList(&deviceCount, 1000);

    std::printf("Dispositivos encontrados: %u\n", deviceCount);

    if (static_cast<uint32_t>(deviceIndex) >= deviceCount)
    {
        std::fprintf(stderr,
            "Indice %d invalido - so %u camera(s) detectada(s).\n",
            deviceIndex, deviceCount);
        GXCloseLib();
        return 1;
    }

    const GX_STATUS openStatus = GXOpenDeviceByIndex(
        static_cast<uint32_t>(deviceIndex + 1),
        &g_handle
    );

    if (openStatus != GX_STATUS_SUCCESS)
    {
        std::fprintf(stderr, "Falha ao abrir camera indice %d (status %d)\n",
                     deviceIndex, static_cast<int>(openStatus));
        GXCloseLib();
        return 1;
    }

    //------------------------------------------------------------
    // Formato de pixel: decide se precisa demosaic.
    //------------------------------------------------------------

    int64_t pixelFormat = 0;
    GXGetEnum(g_handle, GX_ENUM_PIXEL_FORMAT, &pixelFormat);

    g_isBayerCamera = isBayerPixelFormat(pixelFormat);

    std::printf("Pixel format bruto (GX_ENUM_PIXEL_FORMAT) = 0x%08llX (%s)\n",
                static_cast<unsigned long long>(pixelFormat),
                g_isBayerCamera ? "Bayer - demosaic sera aplicado" : "assumindo Mono8");

    //------------------------------------------------------------
    // Habilita Gamma de hardware (se a camera suportar - geralmente
    // nao suporta nesta unidade, e nesse caso usamos o gama por
    // software mais abaixo).
    //------------------------------------------------------------

    GXSetBool(g_handle, GX_BOOL_GAMMA_ENABLE, true);

    GXGetFloatRange(g_handle, GX_FLOAT_GAIN, &g_gainRange);
    GXGetFloatRange(g_handle, GX_FLOAT_GAMMA, &g_gammaRange);
    GXGetIntRange(g_handle, GX_INT_CONTRAST_PARAM, &g_contrastRange);

    g_gainSupported = isUsableFloatRange(g_gainRange);
    g_hardwareGammaSupported = isUsableFloatRange(g_gammaRange);
    g_contrastSupported = isUsableIntRange(g_contrastRange);

    std::printf("Gain:     [%.3f .. %.3f] %s\n",
                g_gainRange.dMin, g_gainRange.dMax,
                g_gainSupported ? "" : "(ignorado - range invalido)");

    std::printf("Gamma HW: [%.3f .. %.3f] %s\n",
                g_gammaRange.dMin, g_gammaRange.dMax,
                g_hardwareGammaSupported ? "" :
                "(ignorado - usando gama por software em vez disso)");

    std::printf("Contrast: [%lld .. %lld] %s\n",
                static_cast<long long>(g_contrastRange.nMin),
                static_cast<long long>(g_contrastRange.nMax),
                g_contrastSupported ? "" : "(ignorado - range invalido)");

    rebuildGammaLut();

    //------------------------------------------------------------
    // Valores atuais da camera, para posicionar os sliders no
    // ponto de partida real.
    //------------------------------------------------------------

    double currentGain = g_gainRange.dMin;
    int64_t currentContrast = g_contrastRange.nMin;

    if (g_gainSupported)
    {
        GXGetFloat(g_handle, GX_FLOAT_GAIN, &currentGain);
    }

    if (g_contrastSupported)
    {
        GXGetInt(g_handle, GX_INT_CONTRAST_PARAM, &currentContrast);
    }

    //------------------------------------------------------------
    // Modo de aquisicao continuo e inicia o stream.
    //------------------------------------------------------------

    GXSetEnum(g_handle, GX_ENUM_ACQUISITION_MODE, GX_ACQ_MODE_CONTINUOUS);
    GXSetEnum(g_handle, GX_ENUM_TRIGGER_MODE, GX_TRIGGER_MODE_OFF);

    const GX_STATUS streamStatus = GXStreamOn(g_handle);

    if (streamStatus != GX_STATUS_SUCCESS)
    {
        std::fprintf(stderr, "Falha ao iniciar stream (status %d)\n",
                     static_cast<int>(streamStatus));
        GXCloseDevice(g_handle);
        GXCloseLib();
        return 1;
    }

    //------------------------------------------------------------
    // Janela com largura fixa (kDisplayWidth), altura proporcional.
    // WINDOW_AUTOSIZE ajusta a janela ao tamanho do frame que a
    // gente ja redimensionou manualmente no loop abaixo.
    //------------------------------------------------------------

    const std::string windowName = "Ajuste camera " + std::to_string(deviceIndex);

    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback(windowName, onMouse);

    if (g_gainSupported)
    {
        cv::createTrackbar("Gain", windowName, nullptr, kSliderSteps, onGainChange);
        cv::setTrackbarPos("Gain", windowName, floatToSlider(currentGain, g_gainRange));
    }

    // Gamma por software: sempre disponivel, independente do que
    // a camera suporte.
    cv::createTrackbar("Gamma", windowName, nullptr, kSliderSteps, onSoftGammaChange);
    cv::setTrackbarPos("Gamma", windowName,
                        static_cast<int>((1.0 - kSoftGammaMin) /
                                          (kSoftGammaMax - kSoftGammaMin) * kSliderSteps));

    if (g_contrastSupported)
    {
        const int contrastSliderMax = static_cast<int>(
            g_contrastRange.nMax - g_contrastRange.nMin
        );

        cv::createTrackbar("Contrast", windowName, nullptr, contrastSliderMax, onContrastChange);
        cv::setTrackbarPos("Contrast", windowName, intToSlider(currentContrast, g_contrastRange));
    }

    std::printf("\nControles: clique = marca ponto da ROI (ate 4) | 'z' desfaz ultimo ponto | "
                "'r' limpa tudo | sliders ajustam ao vivo | 'b' alterna ordem Bayer | "
                "'s' imprime valores | 'q'/ESC sai\n\n");

    //------------------------------------------------------------
    // Loop de captura + preview
    //------------------------------------------------------------

    bool running = true;
    cv::Mat colorFrame;
    cv::Mat gammaCorrected;
    cv::Mat displayFrame;

    while (running)
    {
        PGX_FRAME_BUFFER frameBuffer = nullptr;

        const GX_STATUS grabStatus = GXDQBuf(g_handle, &frameBuffer, 1000);

        if (grabStatus == GX_STATUS_SUCCESS &&
            frameBuffer != nullptr &&
            frameBuffer->nStatus == GX_FRAME_STATUS_SUCCESS)
        {
            const cv::Mat raw(
                frameBuffer->nHeight,
                frameBuffer->nWidth,
                CV_8UC1,
                frameBuffer->pImgBuf
            );

            if (!g_nativeSizeKnown)
            {
                g_nativeSize = cv::Size(frameBuffer->nWidth, frameBuffer->nHeight);
                g_displayScale = static_cast<double>(kDisplayWidth) / g_nativeSize.width;
                g_nativeSizeKnown = true;

                std::printf("Resolucao nativa: %dx%d | exibicao: %dx%d (escala %.3f)\n",
                            g_nativeSize.width, g_nativeSize.height,
                            kDisplayWidth,
                            static_cast<int>(g_nativeSize.height * g_displayScale + 0.5),
                            g_displayScale);
            }

            if (g_isBayerCamera)
            {
                cv::cvtColor(raw, colorFrame, g_bayerCodes[g_bayerCodeIndex]);
            }
            else
            {
                cv::cvtColor(raw, colorFrame, cv::COLOR_GRAY2BGR);
            }

            cv::LUT(colorFrame, g_gammaLut, gammaCorrected);

            // Largura fixa (kDisplayWidth), altura proporcional.
            const int displayHeight = static_cast<int>(
                gammaCorrected.rows * g_displayScale + 0.5
            );

            cv::resize(gammaCorrected, displayFrame,
                       cv::Size(kDisplayWidth, displayHeight));

            // Desenha os pontos ja marcados e as linhas conectando-os
            // (fecha o poligono quando os 4 pontos existem).
            if (!g_roiPointsDisplay.empty())
            {
                for (size_t i = 0; i < g_roiPointsDisplay.size(); i++)
                {
                    cv::circle(displayFrame, g_roiPointsDisplay[i], 4,
                               cv::Scalar(0, 255, 255), cv::FILLED);

                    if (i > 0)
                    {
                        cv::line(displayFrame, g_roiPointsDisplay[i - 1],
                                  g_roiPointsDisplay[i], cv::Scalar(0, 255, 255), 2);
                    }
                }

                if (g_roiPointsDisplay.size() == kRoiPointCount)
                {
                    cv::line(displayFrame, g_roiPointsDisplay[kRoiPointCount - 1],
                              g_roiPointsDisplay[0], cv::Scalar(0, 255, 255), 2);
                }
            }

            cv::imshow(windowName, displayFrame);
        }

        if (frameBuffer != nullptr)
        {
            GXQBuf(g_handle, frameBuffer);
        }

        const int key = cv::waitKey(15) & 0xFF;

        if (key == 'q' || key == 27)
        {
            running = false;
        }
        else if (key == 's')
        {
            printCurrentValues();
        }
        else if (key == 'b' && g_isBayerCamera)
        {
            g_bayerCodeIndex = (g_bayerCodeIndex + 1) % kBayerCodeCount;
            std::printf("Ordem Bayer agora: %s\n", g_bayerNames[g_bayerCodeIndex]);
        }
        else if (key == 'r')
        {
            g_roiPointsDisplay.clear();
            g_roiPointsNative.clear();
            std::printf("ROI limpa (todos os pontos removidos).\n");
        }
        else if (key == 'z')
        {
            if (!g_roiPointsDisplay.empty())
            {
                g_roiPointsDisplay.pop_back();
                g_roiPointsNative.pop_back();

                std::printf("Ultimo ponto desfeito. Pontos restantes: %zu/%zu\n",
                             g_roiPointsDisplay.size(), kRoiPointCount);
            }
            else
            {
                std::printf("Nao ha pontos para desfazer.\n");
            }
        }
    }

    GXStreamOff(g_handle);
    GXCloseDevice(g_handle);
    GXCloseLib();

    cv::destroyAllWindows();

    return 0;
}
