#pragma once
 
#include "Common.hpp"
 
#include "WellMasker.hpp"
#include "WellDetection.hpp"
 
#include <unordered_map>
#include <vector>
 
class ImageProcessor
{
public:
 
    explicit ImageProcessor(WellMasker& wellMasker);
 
    // detections e preenchido com uma entrada por poco do
    // layout atual (mesma ordem de layout->wells), sempre
    // que houver um layout carregado para wellsCount. Se
    // nao houver layout, detections fica vazio.
    cv::Mat process(
        const cv::Mat& frame,
        int wellsCount,
        std::vector<WellDetection>& detections
    );
 
private:
 
    // Estado de rastreio de um poco individual. A partir de
    // agora a referencia do poco vazio vem de uma imagem
    // estatica (config/wells_<N>.png), carregada uma unica
    // vez por wellsCount - nao ha mais "frame anterior" por
    // poco, entao so sobra o historico de intensidade.
    struct WellState
    {
        // Media (EMA) da intensidade media dos blobs ja
        // aceitos para este poco. Usada para rejeitar uma
        // nova deteccao cujo valor medio destoa muito do
        // historico (ex: pegar a cauda, mais fraca/mais
        // fina no diff, em vez da cabeca).
        double avgIntensityMean = 0.0;
 
        bool hasIntensityBaseline = false;
 
        // Mascara circular do poco erodida (encolhida) para
        // excluir a borda/menisco da agua - com referencia
        // estatica, essa borda nunca fica identica ao fundo
        // aprendido (reflexo, oscilacao da agua) e vira ruido
        // persistente em toda comparacao. Calculada uma unica
        // vez por poco, na primeira vez que ele e processado.
        cv::Mat erodedMask;
 
        // Numero de pixels validos dentro de erodedMask,
        // calculado junto com ela. Usado para escalar a area
        // minima de deteccao proporcionalmente ao tamanho do
        // poco, em vez de um valor fixo em pixels.
        int maskArea = 0;
 
        bool hasErodedMask = false;
    };
 
    WellMasker& wellMasker_;
 
    // Um estado por poco (chave = Well::id).
    std::unordered_map<int, WellState> wellStates_;
 
    // Imagem de referencia (poco vazio, tons de cinza) por
    // wellsCount, carregada sob demanda de
    // config/wells_<N>.png. Se o arquivo nao existir ou as
    // dimensoes nao baterem com o frame atual, a entrada
    // fica ausente do mapa e a deteccao e pulada para essa
    // configuracao (ver getReferenceFrame()).
    std::unordered_map<int, cv::Mat> referenceFrames_;
 
    // wellsCount para os quais ja tentamos carregar a
    // referencia (existindo ou nao o arquivo), para nao
    // ficar tentando reabrir/revalidar em todo frame.
    std::unordered_map<int, bool> referenceLoadAttempted_;
 
    // Detecta troca de wells_count para descartar
    // baselines antigas (intensidade EMA de outra config).
    int lastWellsCount_;
 
    // Retorna a referencia estatica (tons de cinza) para
    // wellsCount, carregando de disco na primeira chamada.
    // Retorna nullptr se o arquivo config/wells_<N>.png nao
    // existir ou se as dimensoes nao coincidirem com
    // expectedWidth/expectedHeight (dimensoes do frame
    // atual, ja no mesmo espaco de coordenadas do layout).
    const cv::Mat* getReferenceFrame(
        int wellsCount,
        int expectedWidth,
        int expectedHeight
    );
 
    // Piso absoluto de area (protege pocos muito pequenos/
    // recortados nas bordas do frame). O piso real usado e
    // max(kAreaMinFloor, kAreaMinFraction * area_do_poco).
    static constexpr int kAreaMinFloor = 20;
 
    // Fracao minima da area do poco (apos erosao da mascara)
    // que um blob precisa ocupar para ser considerado peixe,
    // nao ruido. 0.01 = 1% da area do poco.
    static constexpr double kAreaMinFraction = 0.01;
 
    // Contra uma referencia estatica (poco vazio) em vez do
    // frame anterior, a diferenca de fundo (compressao do
    // PNG/video, brilho especular na agua) e bem maior e mais
    // persistente do que a diferenca entre dois frames
    // consecutivos - por isso o piso subiu de 9.0 para 30.0.
    // Ajuste este valor observando o video anotado: se ainda
    // pegar ruido de borda, suba mais; se comecar a perder o
    // peixe quando ele fica muito parado/claro, desca.
    static constexpr double kThresholdFloor = 30.0;
 
    // Raio (em pixels) da erosao aplicada a mascara circular
    // do poco antes de qualquer comparacao. Corta a faixa de
    // borda/menisco, onde a agua nunca fica identica a
    // referencia estatica. Escalado como side/24 no .cpp, com
    // piso de 3px - isto aqui e so o piso.
    static constexpr int kMaskErosionMinPixels = 3;
 
    // Tolerancia relativa (fracao) entre o valor medio do
    // blob atual e a media historica do poco. Ex: 0.4 =
    // aceita deteccoes entre 60% e 140% da media. Fora
    // dessa faixa, a deteccao e tratada como ruido/regiao
    // incompativel e descartada.
    static constexpr double kIntensityToleranceFrac = 0.4;
 
    // Peso da nova amostra na media movel exponencial da
    // intensidade media aceita por poco.
    static constexpr double kIntensityEmaAlpha = 0.2;
};