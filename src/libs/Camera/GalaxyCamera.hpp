#pragma once

#include "Common.hpp"

#include <array>

// Wrapper para uma camera Galaxy (Daheng), incluindo o que foi
// calibrado manualmente com a ferramenta tune_camera:
//   - Demosaic (a camera e Bayer, nao mono)
//   - Gain fixo (hardware)
//   - Gama fixo (software, LUT)
//   - Correcao de perspectiva usando 4 pontos de ROI, para
//     compensar a camera nao estar perfeitamente alinhada com
//     o objeto/poco
//
// grab() ja devolve o frame pronto (BGR, corrigido, recortado no
// tamanho de saida configurado em open()) - quem usa esta classe
// nao precisa lidar com nenhum desses detalhes.
//
// ATENCAO - risco de incompatibilidade de versao do SDK: a API do
// GxIAPI varia um pouco entre versoes. As chamadas usadas aqui
// seguem o padrao confirmado por testes reais (GXOpenDeviceByIndex,
// GXDQBuf/GXQBuf, GXSetFloat/GXGetEnum) nesta instalacao especifica.
class GalaxyCamera
{
public:

    GalaxyCamera();

    ~GalaxyCamera();

    // deviceIndex e 0-based (0, 1, 2...); convertido internamente
    // para o indice 1-based que o GxIAPI espera.
    //
    // roiPoints: 4 pontos em coordenadas NATIVAS da camera (a
    // resolucao real capturada, antes de qualquer correcao),
    // na ordem [superior-esquerdo, inferior-esquerdo,
    // inferior-direito, superior-direito] - mesma ordem em que
    // o tune_camera pede para clicar.
    //
    // outputSize: tamanho do frame retangular ja corrigido que
    // grab() vai devolver, independente da resolucao nativa da
    // camera ou do formato do ROI marcado.
    //
    // rotate180: true se esta camera fisica esta montada de
    // cabeca para baixo (imagem espelhada nos dois eixos). A
    // correcao e embutida na propria matriz de perspectiva, sem
    // custo extra por frame (nao faz um cv::rotate() a parte).
    bool open(
        int deviceIndex,
        const std::array<cv::Point2f, 4>& roiPoints,
        cv::Size outputSize,
        bool rotate180 = false
    );

    // Preenche outFrame (CV_8UC3, BGR) com o frame mais recente,
    // ja com demosaic, gama e correcao de perspectiva aplicados,
    // no tamanho definido em open(). Retorna false em timeout,
    // erro do SDK, ou frame corrompido - outFrame fica intocado
    // nesses casos.
    bool grab(
        cv::Mat& outFrame,
        unsigned int timeoutMs = 1000
    );

    void close();

    bool isOpen() const;

private:

    // GX_DEV_HANDLE e um typedef de ponteiro opaco no GxIAPI.h.
    // Usamos void* aqui para nao precisar incluir GxIAPI.h neste
    // header (mantem o .hpp leve para quem so quer usar a classe).
    void* handle_;

    bool streaming_;

    bool isBayer_;

    cv::Mat gammaLut_;

    cv::Mat perspectiveMatrix_;

    cv::Size outputSize_;

    // Gain e gama calibrados com o tune_camera - confirmados
    // identicos nas duas unidades fisicas usadas neste projeto.
    static constexpr double kGain = 18.0;

    static constexpr double kGamma = 1.318;

    // Ordem do padrao Bayer confirmada visualmente com o
    // tune_camera (tecla 'b') nas duas cameras.
    static const int kBayerConversionCode;
};
