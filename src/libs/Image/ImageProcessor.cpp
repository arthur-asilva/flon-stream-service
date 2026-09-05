#include "ImageProcessor.hpp"

#include <algorithm>
#include <cmath>

ImageProcessor::ImageProcessor(WellMasker& wellMasker)
:
wellMasker_(wellMasker),
lastWellsCount_(-1)
{
}

cv::Mat ImageProcessor::process(
    const cv::Mat& frame,
    int wellsCount,
    std::vector<WellDetection>& detections
)
{
    detections.clear();

    //----------------------------------------------------
    // Base do display: tons de cinza convertidos de volta
    // para BGR, igual ao comportamento anterior.
    //----------------------------------------------------

    cv::Mat gray;

    cv::cvtColor(
        frame,
        gray,
        cv::COLOR_BGR2GRAY
    );

    cv::Mat output;

    cv::cvtColor(
        gray,
        output,
        cv::COLOR_GRAY2BGR
    );

    //----------------------------------------------------
    // Sem layout carregado para este wells_count: nao ha
    // coordenadas de poco para rastrear, apenas retorna
    // o frame base (detections fica vazio).
    //----------------------------------------------------

    const WellLayout* layout =
        wellMasker_.getLayout(wellsCount);

    if(!layout)
    {
        return output;
    }

    //----------------------------------------------------
    // Troca de configuracao (ex: 6 -> 96 pocos): os ROIs
    // antigos nao correspondem mais as novas coordenadas,
    // entao descarta todas as baselines.
    //----------------------------------------------------

    if(wellsCount != lastWellsCount_)
    {
        wellStates_.clear();

        lastWellsCount_ = wellsCount;
    }

    //----------------------------------------------------
    // Recorte quadrado ao redor de cada poco = diametro
    // do circulo do layout (2 * raio).
    //----------------------------------------------------

    const int side = layout->radius * 2;

    const cv::Rect frameBounds(
        0,
        0,
        gray.cols,
        gray.rows
    );

    static const cv::Mat kernel =
        cv::getStructuringElement(
            cv::MORPH_ELLIPSE,
            cv::Size(3, 3)
        );

    for(const Well& well : layout->wells)
    {
        // Toda saida deste poco neste frame passa por esta
        // entrada. Comeca como "nao detectado" e so vira
        // detected=true no caminho feliz, la embaixo.
        WellDetection detection;
        detection.wellId = well.id;

        //--------------------------------------------
        // ROI quadrado, recortado nos limites do frame
        //--------------------------------------------

        cv::Rect roi(
            well.x - side / 2,
            well.y - side / 2,
            side,
            side
        );

        roi &= frameBounds;

        if(roi.width <= 0 || roi.height <= 0)
        {
            detections.push_back(detection);

            continue;
        }

        const cv::Mat currentRoi = gray(roi);

        WellState& state = wellStates_[well.id];

        //--------------------------------------------
        // Primeiro frame deste poco: so guarda a
        // baseline, nada para comparar ainda.
        //--------------------------------------------

        if(!state.hasPrevious)
        {
            currentRoi.copyTo(state.previousRoi);

            state.hasPrevious = true;

            detections.push_back(detection);

            continue;
        }

        //--------------------------------------------
        // Diferenca contra o frame anterior DO MESMO
        // poco (nao contra outro poco, nem contra um
        // fundo global).
        //--------------------------------------------

        cv::Mat diff;

        cv::absdiff(
            currentRoi,
            state.previousRoi,
            diff
        );

        //--------------------------------------------
        // Mascara circular do poco: corta a borda/
        // menisco, maior fonte de falso positivo.
        //--------------------------------------------

        const cv::Mat wellMask = layout->mask(roi);

        cv::bitwise_and(
            diff,
            diff,
            diff,
            wellMask
        );

        cv::GaussianBlur(
            diff,
            diff,
            cv::Size(3, 3),
            0
        );

        //--------------------------------------------
        // Threshold: Otsu com piso absoluto. Otsu
        // falha quando nao ha peixe (so ruido).
        //--------------------------------------------

        cv::Mat binary;

        const double otsuValue =
            cv::threshold(
                diff,
                binary,
                0,
                255,
                cv::THRESH_BINARY | cv::THRESH_OTSU
            );

        if(otsuValue < kThresholdFloor)
        {
            cv::threshold(
                diff,
                binary,
                kThresholdFloor,
                255,
                cv::THRESH_BINARY
            );
        }

        // MORPH_OPEN foi removido: com kernel 3x3 ele
        // apaga blobs finos (movimentos pequenos, ex:
        // so a cauda) antes do filtro de area entrar
        // em acao. Em vez disso, medianBlur no binario
        // remove ruido "sal e pimenta" (pixels isolados)
        // sem corroer um blob pequeno mas conectado.
        cv::medianBlur(binary, binary, 3);

        cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

        //--------------------------------------------
        // 1 peixe por poco: o maior componente conexo
        // E o peixe, sem ambiguidade de associacao.
        //--------------------------------------------

        cv::Mat labels;
        cv::Mat stats;
        cv::Mat centroids;

        const int componentsCount =
            cv::connectedComponentsWithStats(
                binary,
                labels,
                stats,
                centroids,
                8
            );

        int bestLabel = -1;
        int bestArea = 0;

        for(int i = 1; i < componentsCount; i++)
        {
            const int area =
                stats.at<int>(i, cv::CC_STAT_AREA);

            if(area > bestArea)
            {
                bestArea = area;
                bestLabel = i;
            }
        }

        //--------------------------------------------
        // Atualiza a baseline do poco para o proximo
        // frame, independente de ter detectado algo.
        //--------------------------------------------

        currentRoi.copyTo(state.previousRoi);

        if(bestLabel < 0 || bestArea < kAreaMin)
        {
            // Nenhum movimento entre este frame e o
            // anterior (peixe parado, ou realmente nao
            // ha peixe ainda). Sem persistencia visual:
            // este poco fica sem marcador neste frame.
            detections.push_back(detection);

            continue;
        }

        //--------------------------------------------
        // Centroide sub-pixel ponderado pela
        // intensidade da diferenca (reduz jitter em
        // ROIs pequenos).
        //--------------------------------------------

        const cv::Mat blobMask = (labels == bestLabel);

        cv::Mat weighted;

        diff.copyTo(weighted, blobMask);

        const cv::Moments blobMoments =
            cv::moments(weighted, false);

        if(blobMoments.m00 <= 0.0)
        {
            detections.push_back(detection);

            continue;
        }

        const double centroidX =
            blobMoments.m10 / blobMoments.m00;

        const double centroidY =
            blobMoments.m01 / blobMoments.m00;

        //--------------------------------------------
        // Filtro de valor medio: a nova deteccao so e
        // aceita se o valor medio do blob for compativel
        // com o historico do poco. Isso descarta regioes
        // com contraste muito diferente do que vem sendo
        // rastreado (ex: pegar a cauda, mais fraca no
        // diff, em vez da cabeca).
        //--------------------------------------------

        const double avgIntensity =
            blobMoments.m00 / static_cast<double>(bestArea);

        if(state.hasIntensityBaseline)
        {
            const double deviation =
                std::abs(avgIntensity - state.avgIntensityMean) /
                state.avgIntensityMean;

            if(deviation > kIntensityToleranceFrac)
            {
                // Regiao incompativel com o historico:
                // trata como deteccao invalida.
                detections.push_back(detection);

                continue;
            }
        }

        //--------------------------------------------
        // Bounding box quadrado ao redor da regiao
        // detectada (lado = maior dimensao do blob).
        //--------------------------------------------

        const int blobLeft =
            stats.at<int>(bestLabel, cv::CC_STAT_LEFT);

        const int blobTop =
            stats.at<int>(bestLabel, cv::CC_STAT_TOP);

        const int blobWidth =
            stats.at<int>(bestLabel, cv::CC_STAT_WIDTH);

        const int blobHeight =
            stats.at<int>(bestLabel, cv::CC_STAT_HEIGHT);

        const int squareSide =
            std::max(blobWidth, blobHeight);

        const int blobCenterX = blobLeft + blobWidth / 2;
        const int blobCenterY = blobTop + blobHeight / 2;

        //--------------------------------------------
        // Converte coordenadas locais do ROI para
        // coordenadas do frame completo.
        //--------------------------------------------

        const cv::Point2f centroidFull(
            static_cast<float>(roi.x + centroidX),
            static_cast<float>(roi.y + centroidY)
        );

        const cv::Rect squareBoxFull(
            roi.x + blobCenterX - squareSide / 2,
            roi.y + blobCenterY - squareSide / 2,
            squareSide,
            squareSide
        );

        //--------------------------------------------
        // Atualiza a media (EMA) de intensidade aceita
        // do poco, usada no filtro do proximo frame.
        //--------------------------------------------

        if(!state.hasIntensityBaseline)
        {
            state.avgIntensityMean = avgIntensity;

            state.hasIntensityBaseline = true;
        }
        else
        {
            state.avgIntensityMean =
                kIntensityEmaAlpha * avgIntensity +
                (1.0 - kIntensityEmaAlpha) * state.avgIntensityMean;
        }

        //--------------------------------------------
        // Registra a deteccao deste frame (usada pelo
        // RecordingManager para o coordinates.csv).
        //--------------------------------------------

        detection.detected = true;
        detection.x = static_cast<double>(centroidFull.x);
        detection.y = static_cast<double>(centroidFull.y);

        detections.push_back(detection);

        //--------------------------------------------
        // Desenho: bbox quadrado + ponto no centroide
        //--------------------------------------------

        cv::rectangle(
            output,
            squareBoxFull,
            cv::Scalar(0, 255, 0),
            2,
            cv::LINE_AA
        );

        cv::circle(
            output,
            centroidFull,
            3,
            cv::Scalar(0, 0, 255),
            cv::FILLED,
            cv::LINE_AA
        );
    }

    return output;
}
