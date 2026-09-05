// Diagnostico standalone - NAO faz parte do projeto ZSafe.
// Lista todas as cameras Galaxy detectadas na enumeracao, com
// modelo, numero de serie e status de acesso de cada uma.
//
// Objetivo: descobrir se os indices 0 e 1 (GXOpenDeviceByIndex 1 e 2)
// sao duas cameras FISICAS diferentes, ou a MESMA camera aparecendo
// duas vezes na enumeracao (ex: visivel por mais de uma interface de
// rede) - o que explicaria o erro GX_STATUS_INVALID_ACCESS (-8) ao
// tentar abrir a segunda.
//
// Compilar (ajuste os caminhos -I/-L conforme necessario - devem
// bater com os mesmos usados pelo CMakeLists.txt do projeto):
//
//   g++ -std=c++17 diag_cameras.cpp -o diag_cameras \
//       -I/usr/local/include -I/usr/include \
//       -L/usr/lib -L/usr/local/lib -lgxiapi
//
// Rodar:
//
//   ./diag_cameras

#include <GxIAPI.h>

#include <cstdio>
#include <vector>

int main()
{
    GXInitLib();

    uint32_t deviceCount = 0;
    GXUpdateDeviceList(&deviceCount, 1000);

    std::printf("Dispositivos encontrados: %u\n\n", deviceCount);

    if (deviceCount == 0)
    {
        GXCloseLib();
        return 0;
    }

    // Primeira chamada com pDeviceInfo=NULL: descobre o tamanho
    // de buffer necessario.
    size_t bufferSize = 0;
    GXGetAllDeviceBaseInfo(nullptr, &bufferSize);

    const size_t infoCount = bufferSize / sizeof(GX_DEVICE_BASE_INFO);
    std::vector<GX_DEVICE_BASE_INFO> infos(infoCount);

    size_t fillSize = bufferSize;
    const GX_STATUS status = GXGetAllDeviceBaseInfo(infos.data(), &fillSize);

    if (status != GX_STATUS_SUCCESS)
    {
        std::printf("Erro ao obter informacoes dos dispositivos: status %d\n",
                     static_cast<int>(status));
        GXCloseLib();
        return 1;
    }

    for (size_t i = 0; i < infos.size(); i++)
    {
        const GX_DEVICE_BASE_INFO& info = infos[i];

        const char* accessStr = "desconhecido";

        switch (info.accessStatus)
        {
            case GX_ACCESS_STATUS_READWRITE:
                accessStr = "READWRITE (livre para abrir)";
                break;
            case GX_ACCESS_STATUS_READONLY:
                accessStr = "READONLY (ja em uso por outro handle/processo)";
                break;
            case GX_ACCESS_STATUS_NOACCESS:
                accessStr = "NOACCESS (sem acesso)";
                break;
            case GX_ACCESS_STATUS_UNKNOWN:
            default:
                accessStr = "UNKNOWN";
                break;
        }

        std::printf("Indice %zu (GXOpenDeviceByIndex usa indice %zu):\n",
                     i, i + 1);
        std::printf("  Modelo:        %s\n", info.szModelName);
        std::printf("  Numero serie:  %s\n", info.szSN);
        std::printf("  Device ID:     %s\n", info.szDeviceID);
        std::printf("  Classe (raw):  %d\n", static_cast<int>(info.deviceClass));
        std::printf("  Acesso:        %s\n\n", accessStr);
    }

    GXCloseLib();

    return 0;
}
